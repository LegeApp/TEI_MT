#include "translator_llama.hpp"

#include <llama.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::once_flag g_backend_once;

void llama_log_quiet(enum ggml_log_level level, const char* text, void* /*user_data*/) {
    if (level == GGML_LOG_LEVEL_ERROR && text != nullptr) {
        std::fputs(text, stderr);
    }
}

void initialize_backend_once() {
    std::call_once(g_backend_once, []() {
        llama_log_set(llama_log_quiet, nullptr);
        ggml_backend_load_all();
        llama_backend_init();
    });
}

std::string trim(std::string s) {
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };

    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }

    return s;
}

}  // namespace

struct LlamaTranslator::SharedModel {
    explicit SharedModel(const LlamaTranslatorConfig& config) {
        initialize_backend_once();

        llama_model_params params = llama_model_default_params();
        params.n_gpu_layers = config.n_gpu_layers;
        params.main_gpu = 0;
        params.use_mmap = true;

        model = llama_model_load_from_file(config.model_path.c_str(), params);
        if (model == nullptr) {
            throw std::runtime_error("llama_model_load_from_file failed for: " + config.model_path);
        }

        vocab = llama_model_get_vocab(model);
        if (vocab == nullptr) {
            throw std::runtime_error("llama_model_get_vocab returned null");
        }
    }

    ~SharedModel() {
        if (model != nullptr) {
            llama_model_free(model);
            model = nullptr;
            vocab = nullptr;
        }
    }

    llama_model* model = nullptr;
    const llama_vocab* vocab = nullptr;
};

LlamaTranslator::LlamaTranslator(LlamaTranslatorConfig config)
    : config_(std::move(config)), shared_model_(load_shared_model(config_)) {
    prompt_prefix_tokens_ = tokenize(
        "Translate the following Classical Chinese Buddhist passage into natural English.\n"
        "Output English only. Do not explain.\n\n",
        true,
        true
    );
    prompt_suffix_tokens_ = tokenize("\n\nEnglish:\n", false, true);
}

LlamaTranslator::LlamaTranslator(LlamaTranslatorConfig config, std::shared_ptr<SharedModel> shared_model)
    : config_(std::move(config)), shared_model_(std::move(shared_model)) {
    prompt_prefix_tokens_ = tokenize(
        "Translate the following Classical Chinese Buddhist passage into natural English.\n"
        "Output English only. Do not explain.\n\n",
        true,
        true
    );
    prompt_suffix_tokens_ = tokenize("\n\nEnglish:\n", false, true);
}

LlamaTranslator::~LlamaTranslator() {
    if (sampler_ != nullptr) {
        llama_sampler_free(sampler_);
        sampler_ = nullptr;
    }

    if (ctx_ != nullptr) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
}

std::shared_ptr<LlamaTranslator::SharedModel> LlamaTranslator::load_shared_model(const LlamaTranslatorConfig& config) {
    return std::make_shared<SharedModel>(config);
}

void LlamaTranslator::ensure_context_ready() {
    if (ctx_ != nullptr && sampler_ != nullptr) {
        return;
    }

    llama_context_params params = llama_context_default_params();
    params.n_ctx = static_cast<uint32_t>(std::max(512, config_.n_ctx));
    params.n_batch = params.n_ctx;
    params.n_ubatch = params.n_ctx;
    params.n_threads = std::max(1, config_.n_threads);
    params.n_threads_batch = std::max(1, config_.n_threads);
    params.offload_kqv = true;
    params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_AUTO;
    params.no_perf = true;

    ctx_ = llama_init_from_model(shared_model_->model, params);
    if (ctx_ == nullptr) {
        throw std::runtime_error("llama_init_from_model failed");
    }

    auto sparams = llama_sampler_chain_default_params();
    sparams.no_perf = true;
    sampler_ = llama_sampler_chain_init(sparams);
    if (sampler_ == nullptr) {
        throw std::runtime_error("llama_sampler_chain_init failed");
    }

    llama_sampler_chain_add(sampler_, llama_sampler_init_greedy());
}

std::unique_ptr<Translator> LlamaTranslator::clone() const {
    return std::unique_ptr<Translator>(new LlamaTranslator(config_, shared_model_));
}

std::vector<int32_t> LlamaTranslator::tokenize(const std::string& text, bool add_special, bool parse_special) const {
    const int32_t required = -llama_tokenize(
        shared_model_->vocab,
        text.c_str(),
        static_cast<int32_t>(text.size()),
        nullptr,
        0,
        add_special,
        parse_special
    );

    if (required <= 0) {
        throw std::runtime_error("llama_tokenize failed while querying required token count");
    }

    std::vector<llama_token> tokens(static_cast<std::size_t>(required));
    const int32_t written = llama_tokenize(
        shared_model_->vocab,
        text.c_str(),
        static_cast<int32_t>(text.size()),
        tokens.data(),
        static_cast<int32_t>(tokens.size()),
        add_special,
        parse_special
    );

    if (written < 0) {
        throw std::runtime_error("llama_tokenize failed while writing tokens");
    }

    tokens.resize(static_cast<std::size_t>(written));
    return std::vector<int32_t>(tokens.begin(), tokens.end());
}

std::string LlamaTranslator::token_to_piece(int32_t token) const {
    char local[256];
    const int first = llama_token_to_piece(
        shared_model_->vocab,
        static_cast<llama_token>(token),
        local,
        static_cast<int32_t>(sizeof(local)),
        0,
        true
    );

    if (first >= 0) {
        return std::string(local, static_cast<std::size_t>(first));
    }

    std::vector<char> dynamic(static_cast<std::size_t>(-first));
    const int second = llama_token_to_piece(
        shared_model_->vocab,
        static_cast<llama_token>(token),
        dynamic.data(),
        static_cast<int32_t>(dynamic.size()),
        0,
        true
    );

    if (second < 0) {
        throw std::runtime_error("llama_token_to_piece failed");
    }

    return std::string(dynamic.data(), static_cast<std::size_t>(second));
}

std::string LlamaTranslator::postprocess_translation(std::string text) const {
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());

    const std::string marker = "English:";
    const std::size_t marker_pos = text.find(marker);
    if (marker_pos != std::string::npos) {
        text = text.substr(marker_pos + marker.size());
    }

    const std::size_t dbl_newline = text.find("\n\n");
    if (dbl_newline != std::string::npos) {
        text = text.substr(0, dbl_newline);
    }

    return trim(std::move(text));
}

bool LlamaTranslator::has_early_stop_marker(const std::string& generated) const {
    return generated.find("\n\n") != std::string::npos;
}

std::string LlamaTranslator::translate(const Segment& segment) {
    ensure_context_ready();

    llama_memory_clear(llama_get_memory(ctx_), true);
    llama_sampler_reset(sampler_);

    const std::vector<int32_t> segment_tokens_i32 = tokenize(segment.source_zh, false, true);
    std::vector<int32_t> prompt_tokens_i32;
    prompt_tokens_i32.reserve(prompt_prefix_tokens_.size() + segment_tokens_i32.size() + prompt_suffix_tokens_.size());
    prompt_tokens_i32.insert(prompt_tokens_i32.end(), prompt_prefix_tokens_.begin(), prompt_prefix_tokens_.end());
    prompt_tokens_i32.insert(prompt_tokens_i32.end(), segment_tokens_i32.begin(), segment_tokens_i32.end());
    prompt_tokens_i32.insert(prompt_tokens_i32.end(), prompt_suffix_tokens_.begin(), prompt_suffix_tokens_.end());

    if (prompt_tokens_i32.empty()) {
        throw std::runtime_error("Prompt tokenization produced no tokens");
    }

    std::vector<llama_token> prompt_tokens(prompt_tokens_i32.begin(), prompt_tokens_i32.end());

    const uint32_t n_ctx_actual = llama_n_ctx(ctx_);
    if (prompt_tokens.size() + static_cast<std::size_t>(std::max(1, config_.max_tokens)) >= n_ctx_actual) {
        throw std::runtime_error(
            "Prompt too long for context window (prompt_tokens=" + std::to_string(prompt_tokens.size()) +
            ", n_ctx=" + std::to_string(n_ctx_actual) + ")"
        );
    }

    if (llama_model_has_encoder(shared_model_->model)) {
        llama_batch enc_batch = llama_batch_get_one(prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size()));
        if (llama_encode(ctx_, enc_batch) != 0) {
            throw std::runtime_error("llama_encode failed");
        }

        llama_token decoder_start = llama_model_decoder_start_token(shared_model_->model);
        if (decoder_start == LLAMA_TOKEN_NULL) {
            decoder_start = llama_vocab_bos(shared_model_->vocab);
        }

        llama_batch dec_batch = llama_batch_get_one(&decoder_start, 1);
        std::string generated;

        for (int i = 0; i < std::max(1, config_.max_tokens); ++i) {
            if (llama_decode(ctx_, dec_batch) != 0) {
                throw std::runtime_error("llama_decode failed during encoder-decoder generation");
            }

            llama_token tok = llama_sampler_sample(sampler_, ctx_, -1);
            if (llama_vocab_is_eog(shared_model_->vocab, tok)) {
                break;
            }

            generated += token_to_piece(tok);
            if (has_early_stop_marker(generated)) {
                break;
            }
            dec_batch = llama_batch_get_one(&tok, 1);
        }

        return postprocess_translation(std::move(generated));
    }

    llama_batch batch = llama_batch_get_one(prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size()));
    if (llama_decode(ctx_, batch) != 0) {
        throw std::runtime_error("llama_decode failed for prompt");
    }

    std::string generated;

    for (int i = 0; i < std::max(1, config_.max_tokens); ++i) {
        const llama_token tok = llama_sampler_sample(sampler_, ctx_, -1);
        if (llama_vocab_is_eog(shared_model_->vocab, tok)) {
            break;
        }

        generated += token_to_piece(tok);
        if (has_early_stop_marker(generated)) {
            break;
        }

        if (i + 1 >= std::max(1, config_.max_tokens)) {
            break;
        }

        llama_token next = tok;
        batch = llama_batch_get_one(&next, 1);
        if (llama_decode(ctx_, batch) != 0) {
            throw std::runtime_error("llama_decode failed for continuation token");
        }
    }

    return postprocess_translation(std::move(generated));
}
