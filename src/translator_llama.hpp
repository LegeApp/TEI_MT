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
    int n_gpu_layers = -1;
    int n_threads = 8;
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

    std::string postprocess_translation(std::string text) const;
    bool has_early_stop_marker(const std::string& generated) const;

    std::vector<int32_t> tokenize(const std::string& text, bool add_special, bool parse_special) const;
    std::string token_to_piece(int32_t token) const;

    void ensure_context_ready();

    LlamaTranslatorConfig config_;
    std::shared_ptr<SharedModel> shared_model_;
    std::vector<int32_t> prompt_prefix_tokens_;
    std::vector<int32_t> prompt_suffix_tokens_;

    llama_context* ctx_ = nullptr;
    llama_sampler* sampler_ = nullptr;
};
