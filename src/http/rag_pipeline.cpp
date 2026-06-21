#include "http/rag_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>

#include "http/mcp/json.hpp"

namespace muduo_http {

// libcurl write callback
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), total);
    return total;
}

// -------- Embedder --------

Embedder::Embedder(std::shared_ptr<AiGateway> gateway)
    : gateway_(std::move(gateway)) {}

std::vector<float> Embedder::Embed(const std::string& text) {
    // Build embedding request using raw HTTP
    // OpenAI Embedding API: POST /embeddings
    // We use AiGateway's internal libcurl to call it
    // Since AiGateway doesn't have a generic HTTP method, we create a mini request

    CURL* curl = curl_easy_init();
    if (!curl) return {};

    std::string url = gateway_->config().api_base + "/embeddings";
    std::string response_body;

    // Build JSON body
    nlohmann::json body;
    body["model"] = model_;
    body["input"] = text;

    std::string body_str = body.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers,
        ("Authorization: Bearer " + gateway_->config().api_key).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_str.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[embed] curl error: " << curl_easy_strerror(res) << '\n';
        return {};
    }

    // Parse response to extract embedding vector
    try {
        auto json = nlohmann::json::parse(response_body);
        auto& data = json["data"];
        if (data.empty()) return {};

        auto& embedding = data[0]["embedding"];
        std::vector<float> result;
        for (auto& val : embedding) {
            result.push_back(val.get<float>());
        }
        return result;
    } catch (const std::exception& e) {
        std::cerr << "[embed] parse error: " << e.what() << '\n';
        return {};
    }
}

std::vector<std::vector<float>> Embedder::EmbedBatch(const std::vector<std::string>& texts) {
    std::vector<std::vector<float>> results;
    for (const auto& text : texts) {
        auto emb = Embed(text);
        if (!emb.empty()) {
            results.push_back(emb);
        }
    }
    return results;
}

// -------- VectorStore --------

void VectorStore::Add(const DocChunk& chunk) {
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.push_back(chunk);
}

void VectorStore::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.clear();
}

std::vector<DocChunk> VectorStore::Search(const std::vector<float>& query_embedding, int top_k) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (chunks_.empty() || query_embedding.empty()) return {};

    // Compute similarity for all chunks
    std::vector<std::pair<float, int>> scores;
    for (int i = 0; i < static_cast<int>(chunks_.size()); ++i) {
        float sim = CosineSimilarity(query_embedding, chunks_[i].embedding);
        scores.push_back({sim, i});
    }

    // Sort by similarity descending
    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    // Take top-k
    std::vector<DocChunk> results;
    for (int i = 0; i < std::min(top_k, static_cast<int>(scores.size())); ++i) {
        if (scores[i].first > 0.0f) {  // Only include positive matches
            results.push_back(chunks_[scores[i].second]);
        }
    }
    return results;
}

bool VectorStore::Save(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& chunk : chunks_) {
            nlohmann::json emb = nlohmann::json::array();
            for (float v : chunk.embedding) emb.push_back(v);
            j.push_back({
                {"id", chunk.id},
                {"text", chunk.text},
                {"embedding", emb},
                {"source", chunk.source}
            });
        }
        std::ofstream file(filepath);
        file << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

bool VectorStore::Load(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        nlohmann::json j;
        file >> j;

        chunks_.clear();
        for (auto& item : j) {
            DocChunk chunk;
            chunk.id = item["id"];
            chunk.text = item["text"];
            chunk.source = item.value("source", "");
            for (auto& v : item["embedding"]) {
                chunk.embedding.push_back(v.get<float>());
            }
            chunks_.push_back(chunk);
        }
        return true;
    } catch (...) {
        return false;
    }
}

float VectorStore::CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;

    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    if (norm_a == 0.0f || norm_b == 0.0f) return 0.0f;
    return dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

// -------- RAGPipeline --------

RAGPipeline::RAGPipeline(std::shared_ptr<Embedder> embedder,
                         std::shared_ptr<VectorStore> store,
                         std::shared_ptr<AiGateway> llm_gateway)
    : embedder_(std::move(embedder)),
      store_(std::move(store)),
      llm_gateway_(std::move(llm_gateway)) {}

std::string RAGPipeline::Query(const std::string& question, int top_k) {
    // 1. Embed the question
    auto query_emb = embedder_->Embed(question);
    if (query_emb.empty()) {
        return "Error: failed to embed question";
    }

    // 2. Retrieve relevant chunks
    auto chunks = store_->Search(query_emb, top_k);

    // 3. Build augmented prompt
    std::string context;
    for (size_t i = 0; i < chunks.size(); ++i) {
        context += "[" + std::to_string(i + 1) + "] " + chunks[i].text + "\n";
    }

    std::string system_prompt =
        "You are a helpful assistant. Answer the user's question based on the "
        "following context. If the context doesn't contain enough information, "
        "say so.\n\nContext:\n" + context;

    // 4. Call LLM with augmented prompt
    AiChatRequest req;
    req.messages.push_back({"system", system_prompt});
    req.messages.push_back({"user", question});

    auto result = llm_gateway_->Chat(req);
    if (result.success) {
        return result.content;
    }
    return "Error: " + result.error_message;
}

void RAGPipeline::AddDocument(const std::string& text, const std::string& source) {
    // Embed the full text
    auto emb = embedder_->Embed(text);
    if (emb.empty()) {
        std::cerr << "[rag] failed to embed document\n";
        return;
    }

    DocChunk chunk;
    chunk.id = "doc_" + std::to_string(store_->size() + 1);
    chunk.text = text;
    chunk.embedding = emb;
    chunk.source = source;
    store_->Add(chunk);

    std::cout << "[rag] added document (" << text.size() << " chars)\n";
}

void RAGPipeline::AddDocumentChunked(const std::string& text, const std::string& source,
                                      int chunk_size, int chunk_overlap) {
    // Split text into chunks
    std::vector<std::string> chunks;
    int start = 0;
    while (start < static_cast<int>(text.size())) {
        int end = std::min(start + chunk_size, static_cast<int>(text.size()));
        // Try to break at a word boundary
        if (end < static_cast<int>(text.size())) {
            auto pos = text.rfind(' ', end);
            if (pos > static_cast<size_t>(start)) end = static_cast<int>(pos);
        }
        chunks.push_back(text.substr(start, end - start));
        start = end - chunk_overlap;
        if (start < 0) start = 0;
    }

    std::cout << "[rag] splitting into " << chunks.size() << " chunks\n";

    // Embed and store each chunk
    for (size_t i = 0; i < chunks.size(); ++i) {
        auto emb = embedder_->Embed(chunks[i]);
        if (emb.empty()) continue;

        DocChunk chunk;
        chunk.id = source + "_" + std::to_string(i);
        chunk.text = chunks[i];
        chunk.embedding = emb;
        chunk.source = source;
        store_->Add(chunk);
    }
}

} // namespace muduo_http
