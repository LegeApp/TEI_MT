#pragma once

#include "translator.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct llama_model;
struct llama_context;
struct llama_sampler;
struct llama_vocab;

struct LlamaTranslatorConfig {
    std::string model_path;
    int n_ctx = 2048;
    /// Upper bound when auto-growing context to fit a prompt (VRAM / driver limits).
    int max_n_ctx = 131072;
    int n_gpu_layers = -1;
    int n_threads = 0;
    int max_tokens = 192;
};

class LlamaTranslator final : public Translator {
public:
    explicit LlamaTranslator(LlamaTranslatorConfig config);
    ~LlamaTranslator() override;

    std::unique_ptr<Translator> clone() const override;
    std::string translate(const Segment& segment) override;

private:
    struct SharedModel;

    LlamaTranslator(LlamaTranslatorConfig config, std::shared_ptr<SharedModel> shared_model);

    static std::shared_ptr<SharedModel> load_shared_model(const LlamaTranslatorConfig& config);

    std::string postprocess_translation(std::string text, bool coalesced_output) const;
    bool has_early_stop_marker(const std::string& generated) const;

    std::vector<int32_t> tokenize(const std::string& text, bool add_special, bool parse_special) const;
    void tokenize_into(const std::string& text, bool add_special, bool parse_special, std::vector<int32_t>& out) const;
    std::string token_to_piece(int32_t token) const;

    void ensure_context_ready();
    void release_context_resources();
    /// Grow config_.n_ctx (recreate llama context) so prompt + generation can fit. Returns false if already at max.
    bool bump_ctx_capacity(std::size_t prompt_tokens, int generation_need);

    LlamaTranslatorConfig config_;
    std::shared_ptr<SharedModel> shared_model_;
    std::vector<int32_t> prompt_prefix_tokens_;
    std::vector<int32_t> prompt_prefix_multi_tokens_;
    std::vector<int32_t> prompt_suffix_tokens_;

    std::vector<int32_t> segment_tokens_scratch_;
    std::vector<int32_t> prompt_i32_scratch_;

    uint32_t ctx_n_batch_ = 512;

    llama_context* ctx_ = nullptr;
    llama_sampler* sampler_ = nullptr;
};
