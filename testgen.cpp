#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Config {
    std::size_t total_ops = 1000;
    std::size_t unique_keys = 100;
    int key_len_min = 4;
    int key_len_max = 12;
    double p_insert = 0.45;
    double p_delete = 0.25;
    double p_find = 0.30;
    double existing_delete_rate = 0.7;  // Chance that delete targets existing entry.
    unsigned int seed = static_cast<unsigned int>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
};

struct ArgsParser {
    static void show_usage() {
        std::cerr << "Usage: test_gen [options]\n"
                  << "  --ops N                Total operations (default 1000)\n"
                  << "  --keys N               Max distinct keys (default 100)\n"
                  << "  --min-len N            Min key length (default 4)\n"
                  << "  --max-len N            Max key length (default 12)\n"
                  << "  --p-insert F           Insert probability (default 0.45)\n"
                  << "  --p-delete F           Delete probability (default 0.25)\n"
                  << "  --p-find F             Find probability (default 0.30)\n"
                  << "  --existing-delete F    Delete hits existing entry with prob F (default 0.7)\n"
                  << "  --seed N               RNG seed (default: time based)\n"
                  << "  -h, --help             Show this help\n";
    }

    static bool parse(int argc, char** argv, Config& cfg) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto needs_value = [&](int idx) { return idx + 1 < argc; };
            if (arg == "-h" || arg == "--help") {
                show_usage();
                return false;
            } else if (arg == "--ops" && needs_value(i)) {
                cfg.total_ops = static_cast<std::size_t>(std::stoull(argv[++i]));
            } else if (arg == "--keys" && needs_value(i)) {
                cfg.unique_keys = static_cast<std::size_t>(std::stoull(argv[++i]));
            } else if (arg == "--min-len" && needs_value(i)) {
                cfg.key_len_min = std::stoi(argv[++i]);
            } else if (arg == "--max-len" && needs_value(i)) {
                cfg.key_len_max = std::stoi(argv[++i]);
            } else if (arg == "--p-insert" && needs_value(i)) {
                cfg.p_insert = std::stod(argv[++i]);
            } else if (arg == "--p-delete" && needs_value(i)) {
                cfg.p_delete = std::stod(argv[++i]);
            } else if (arg == "--p-find" && needs_value(i)) {
                cfg.p_find = std::stod(argv[++i]);
            } else if (arg == "--existing-delete" && needs_value(i)) {
                cfg.existing_delete_rate = std::stod(argv[++i]);
            } else if (arg == "--seed" && needs_value(i)) {
                cfg.seed = static_cast<unsigned int>(std::stoul(argv[++i]));
            } else {
                std::cerr << "Unknown or incomplete argument: " << arg << "\n";
                show_usage();
                return false;
            }
        }
        double sum = cfg.p_insert + cfg.p_delete + cfg.p_find;
        if (sum <= 0.0) {
            std::cerr << "Probabilities must sum to a positive value.\n";
            return false;
        }
        cfg.p_insert /= sum;
        cfg.p_delete /= sum;
        cfg.p_find /= sum;
        cfg.key_len_min = std::max(1, cfg.key_len_min);
        cfg.key_len_max = std::max(cfg.key_len_min, cfg.key_len_max);
        cfg.unique_keys = std::max<std::size_t>(1, cfg.unique_keys);
        cfg.total_ops = std::max<std::size_t>(1, cfg.total_ops);
        if (cfg.existing_delete_rate < 0.0) cfg.existing_delete_rate = 0.0;
        if (cfg.existing_delete_rate > 1.0) cfg.existing_delete_rate = 1.0;
        return true;
    }
};

class Generator {
public:
    explicit Generator(const Config& cfg)
        : cfg_(cfg), rng_(cfg.seed), dist_prob_(0.0, 1.0), dist_char_(0, chars_.size() - 1),
          dist_int_(std::numeric_limits<int>::min(), std::numeric_limits<int>::max()) {}

    void run() {
        std::vector<std::string> key_pool;
        key_pool.reserve(cfg_.unique_keys);

        for (std::size_t i = 0; i < cfg_.total_ops; ++i) {
            double p = dist_prob_(rng_);
            if (p < cfg_.p_insert) {
                emit_insert(key_pool);
            } else if (p < cfg_.p_insert + cfg_.p_delete) {
                emit_delete(key_pool);
            } else {
                emit_find(key_pool);
            }
        }
    }

private:
    void emit_insert(std::vector<std::string>& key_pool) {
        std::string key = pick_or_make_key(key_pool, /*favor_new=*/true);

        int value = dist_int_(rng_);
        auto& bucket = state_[key];
        if (!bucket.empty()) {
            for (int attempt = 0; attempt < 8; ++attempt) {
                if (bucket.count(value) == 0) break;
                value = dist_int_(rng_);
            }
            while (bucket.count(value)) {
                value = dist_int_(rng_);
            }
        }
        bucket.insert(value);
        std::cout << "insert " << key << ' ' << value << '\n';
    }

    void emit_delete(std::vector<std::string>& key_pool) {
        bool hit_existing = dist_prob_(rng_) < cfg_.existing_delete_rate && !state_.empty();
        std::string key;
        int value = dist_int_(rng_);

        if (hit_existing) {
            key = pick_existing_key();
            auto& bucket = state_[key];
            if (!bucket.empty()) {
                auto it = bucket.begin();
                std::advance(it, static_cast<long>(dist_prob_(rng_) * bucket.size()));
                value = *it;
                bucket.erase(it);
                if (bucket.empty()) {
                    state_.erase(key);
                }
            }
        } else {
            key = pick_or_make_key(key_pool, /*favor_new=*/false);
        }

        std::cout << "delete " << key << ' ' << value << '\n';
    }

    void emit_find(std::vector<std::string>& key_pool) {
        std::string key;
        if (!state_.empty() && dist_prob_(rng_) < 0.6) {
            key = pick_existing_key();
        } else {
            key = pick_or_make_key(key_pool, /*favor_new=*/false);
        }
        std::cout << "find " << key << '\n';
    }

    std::string pick_existing_key() {
        std::uniform_int_distribution<std::size_t> dist(0, state_.size() - 1);
        auto it = state_.begin();
        std::advance(it, static_cast<long>(dist(rng_)));
        return it->first;
    }

    std::string pick_or_make_key(std::vector<std::string>& key_pool, bool favor_new) {
        bool can_make_new = key_pool.size() < cfg_.unique_keys;
        bool make_new = favor_new && can_make_new && dist_prob_(rng_) < 0.65;
        if (!make_new && !key_pool.empty()) {
            std::uniform_int_distribution<std::size_t> dist(0, key_pool.size() - 1);
            return key_pool[dist(rng_)];
        }
        std::string key = random_key();
        if (key_pool.size() < cfg_.unique_keys) {
            key_pool.push_back(key);
        }
        return key;
    }

    std::string random_key() {
        std::uniform_int_distribution<int> len_dist(cfg_.key_len_min, cfg_.key_len_max);
        int len = len_dist(rng_);
        std::string s;
        s.reserve(static_cast<std::size_t>(len));
        for (int i = 0; i < len; ++i) {
            s.push_back(chars_[dist_char_(rng_)]);
        }
        return s;
    }

    Config cfg_;
    std::mt19937 rng_;
    std::uniform_real_distribution<double> dist_prob_;
    const std::string chars_ = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::uniform_int_distribution<std::size_t> dist_char_;
    std::uniform_int_distribution<int> dist_int_;
    std::unordered_map<std::string, std::set<int>> state_;
};

int main(int argc, char** argv) {
    Config cfg;
    if (!ArgsParser::parse(argc, argv, cfg)) {
        return 1;
    }
    Generator gen(cfg);
    gen.run();
    return 0;
}
