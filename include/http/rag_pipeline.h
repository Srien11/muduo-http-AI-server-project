#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "http/ai_gateway.h"

namespace muduo_http {

// A single document chunk with its embedding
struct DocChunk {
    std::string id;
    std::string text;
    std::vector<float> embedding;  // 1536-dim for text-embedding-3-small
    std::string source;
};

// -------- Embedder: calls OpenAI Embedding API --------
class Embedder {
public:
    explicit Embedder(std::shared_ptr<AiGateway> gateway);

    // Embed a single text string
    std::vector<float> Embed(const std::string& text);

    // Embed multiple texts in batch
    std::vector<std::vector<float>> EmbedBatch(const std::vector<std::string>& texts);

    int dimension() const { return 1536; }  // text-embedding-3-small

private:
    std::shared_ptr<AiGateway> gateway_;
    std::string model_{"text-embedding-3-small"};
};

// -------- VectorStore: stores and searches chunks --------
class VectorStore {
public:
    VectorStore() = default;

    void Add(const DocChunk& chunk);
    void Clear();

    // Search for top-k most similar chunks
    std::vector<DocChunk> Search(const std::vector<float>& query_embedding, int top_k = 3);

    // Persist to / load from file (JSON format)
    bool Save(const std::string& filepath);
    bool Load(const std::string& filepath);

    size_t size() const { return chunks_.size(); }

private:
    static float CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);

    std::vector<DocChunk> chunks_;
    std::mutex mutex_;
};

// -------- RAGPipeline: full RAG pipeline --------
class RAGPipeline {
public:
    RAGPipeline(std::shared_ptr<Embedder> embedder,
                std::shared_ptr<VectorStore> store,
                std::shared_ptr<AiGateway> llm_gateway);

    // Ask a question with RAG context
    std::string Query(const std::string& question, int top_k = 3);

    // Add a document to the knowledge base
    void AddDocument(const std::string& text, const std::string& source = "");

    // Add a document by splitting into chunks first
    void AddDocumentChunked(const std::string& text, const std::string& source = "",
                            int chunk_size = 500, int chunk_overlap = 50);

    // Stats
    size_t DocumentCount() const { return store_->size(); }

private:
    std::shared_ptr<Embedder> embedder_;
    std::shared_ptr<VectorStore> store_;
    std::shared_ptr<AiGateway> llm_gateway_;
};

} // namespace muduo_http
