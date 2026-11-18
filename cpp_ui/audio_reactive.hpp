#pragma once

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AudioSnapshot {
    float level = 0.0f;
    float bass = 0.0f;
    float treble = 0.0f;
    uint32_t beat_seq = 0;
    float beat_strength = 0.0f;
    bool active = false;
    std::array<float, 8> bands{};
};

class AudioReactiveMonitor {
public:
    AudioReactiveMonitor() = default;
    ~AudioReactiveMonitor() { stop(); }

    AudioReactiveMonitor(const AudioReactiveMonitor&) = delete;
    AudioReactiveMonitor& operator=(const AudioReactiveMonitor&) = delete;

    void set_sensitivity(double sens) {
        sensitivity_.store(static_cast<float>(std::clamp(sens, 0.3, 4.0)));
    }

    void start(const std::string& source_mode) {
        std::string normalized = (source_mode == "mic") ? "mic" : "monitor";
        if (running_.load() && source_mode_ == normalized) return;
        stop();
        source_mode_ = normalized;
        stop_requested_.store(false);
        running_.store(true);
        worker_ = std::thread([this]() { capture_loop(); });
    }

    void stop() {
        stop_requested_.store(true);
        pid_t pid = child_pid_.exchange(-1);
        if (pid > 0) {
            ::kill(pid, SIGTERM);
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        if (pid > 0) {
            int st = 0;
            ::waitpid(pid, &st, WNOHANG);
        }
        running_.store(false);
        std::lock_guard<std::mutex> lk(mu_);
        snap_ = AudioSnapshot{};
    }

    AudioSnapshot snapshot() const {
        std::lock_guard<std::mutex> lk(mu_);
        return snap_;
    }

    // Processes an arbitrary PCM buffer (mono 24kHz s16le) and updates DSP bands & beat detector.
    void process_pcm_samples(const int16_t* samples, size_t count) {
        if (!samples || count == 0) return;

        double sum_sq = 0.0;
        double bass_sq = 0.0;
        double treble_sq = 0.0;
        const double sens = static_cast<double>(sensitivity_.load());

        for (size_t i = 0; i < count; ++i) {
            double s = static_cast<double>(samples[i]) / 32768.0;
            sum_sq += s * s;

            // Simple 1-pole low-pass (~320 Hz @ 24kHz) + high-frequency difference (~1.8 kHz+)
            lp_state_ += 0.082 * (s - lp_state_);
            double hp = s - hp_lp_state_;
            hp_lp_state_ += 0.38 * (s - hp_lp_state_);

            bass_sq += lp_state_ * lp_state_;
            treble_sq += hp * hp;
        }

        double rms = std::sqrt(sum_sq / static_cast<double>(count));
        double bass_rms = std::sqrt(bass_sq / static_cast<double>(count));
        double treble_rms = std::sqrt(treble_sq / static_cast<double>(count));

        // Adaptive peak tracker so quiet music and loud games both stay responsive.
        double raw_energy = std::max(rms, bass_rms * 0.9);
        if (raw_energy > agc_peak_) {
            agc_peak_ = agc_peak_ * 0.72 + raw_energy * 0.28;
        } else {
            agc_peak_ = std::max(0.032, agc_peak_ * 0.994);
        }

        auto norm_scale = [&](double val, double gain) -> float {
            double v = (val * sens * gain) / std::max(0.045, agc_peak_ * 0.85);
            // Suppress tiny floor hum so idle noise floor stays calm
            if (val < 0.0028) v *= 0.2;
            return static_cast<float>(std::clamp(v, 0.0, 1.0));
        };

        float tgt_level = norm_scale(rms, 1.35);
        float tgt_bass = norm_scale(bass_rms, 1.60);
        float tgt_treble = norm_scale(treble_rms, 1.45);

        bass_avg_ = bass_avg_ * 0.90 + bass_rms * 0.10;
        bool is_beat = false;
        float beat_str = 0.0f;
        auto now = std::chrono::steady_clock::now();
        auto ms_since_beat =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_beat_tp_).count();

        if (bass_rms > 0.012 && bass_rms > bass_avg_ * 1.42 && ms_since_beat >= 165) {
            is_beat = true;
            beat_str = std::clamp(tgt_bass, 0.25f, 1.0f);
            last_beat_tp_ = now;
        }

        std::lock_guard<std::mutex> lk(mu_);
        auto smooth = [](float cur, float tgt) -> float {
            float alpha = (tgt > cur) ? 0.56f : 0.16f;
            float out = cur + (tgt - cur) * alpha;
            return (out < 0.005f) ? 0.0f : out;
        };
        snap_.active = true;
        snap_.level = smooth(snap_.level, tgt_level);
        snap_.bass = smooth(snap_.bass, tgt_bass);
        snap_.treble = smooth(snap_.treble, tgt_treble);
        if (is_beat) {
            snap_.beat_seq += 1;
            snap_.beat_strength = beat_str;
        } else {
            snap_.beat_strength *= 0.88f;
        }

        // Shape 8 musical equalizer bands cleanly from bass -> mid -> treble
        static const std::array<float, 8> w_bass = {1.0f, 0.88f, 0.62f, 0.38f, 0.20f, 0.10f, 0.06f, 0.04f};
        static const std::array<float, 8> w_mid = {0.15f, 0.42f, 0.78f, 1.00f, 0.92f, 0.64f, 0.35f, 0.18f};
        static const std::array<float, 8> w_treb = {0.02f, 0.08f, 0.20f, 0.42f, 0.68f, 0.90f, 1.00f, 0.95f};
        for (size_t b = 0; b < 8; ++b) {
            float target_bar = std::clamp(
                snap_.bass * w_bass[b] + snap_.level * w_mid[b] * 0.85f + snap_.treble * w_treb[b],
                0.0f, 1.0f);
            snap_.bands[b] = smooth(snap_.bands[b], target_bar);
        }
    }

private:
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<float> sensitivity_{1.35f};
    std::atomic<pid_t> child_pid_{-1};
    std::string source_mode_ = "monitor";
    std::thread worker_;

    double lp_state_ = 0.0;
    double hp_lp_state_ = 0.0;
    double agc_peak_ = 0.08;
    double bass_avg_ = 0.01;
    std::chrono::steady_clock::time_point last_beat_tp_{};

    mutable std::mutex mu_;
    AudioSnapshot snap_{};

    int spawn_capture_process(bool use_pw_record, pid_t& out_pid) {
        int pipefd[2]{-1, -1};
        if (::pipe2(pipefd, O_CLOEXEC) != 0) return -1;

        pid_t pid = ::fork();
        if (pid < 0) {
            ::close(pipefd[0]);
            ::close(pipefd[1]);
            return -1;
        }
        if (pid == 0) {
            ::close(pipefd[0]);
            ::dup2(pipefd[1], STDOUT_FILENO);
            ::close(pipefd[1]);

            int devnull = ::open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                ::dup2(devnull, STDERR_FILENO);
                ::close(devnull);
            }

            if (!use_pw_record) {
                const char* device = (source_mode_ == "mic") ? "@DEFAULT_SOURCE@" : "@DEFAULT_MONITOR@";
                const char* argv[] = {
                    "parec",
                    "--raw",
                    "--format=s16le",
                    "--channels=1",
                    "--rate=24000",
                    "--latency-msec=20",
                    "-d",
                    device,
                    nullptr,
                };
                ::execvp(argv[0], const_cast<char* const*>(argv));
            } else {
                const char* argv[] = {
                    "pw-record",
                    "--raw",
                    "--format=s16",
                    "--channels=1",
                    "--rate=24000",
                    "-",
                    nullptr,
                };
                ::execvp(argv[0], const_cast<char* const*>(argv));
            }
            ::_exit(127);
        }

        ::close(pipefd[1]);
        out_pid = pid;
        return pipefd[0];
    }

    void capture_loop() {
        bool try_pw = false;
        while (!stop_requested_.load()) {
            pid_t pid = -1;
            int fd = spawn_capture_process(try_pw, pid);
            if (fd < 0 || pid <= 0) {
                try_pw = !try_pw;
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                continue;
            }
            child_pid_.store(pid);

            {
                std::lock_guard<std::mutex> lk(mu_);
                snap_.active = true;
            }

            std::array<int16_t, 512> sample_buf{};
            std::vector<uint8_t> byte_buf(sample_buf.size() * sizeof(int16_t));
            size_t leftover = 0;

            while (!stop_requested_.load()) {
                struct pollfd pfd{};
                pfd.fd = fd;
                pfd.events = POLLIN;
                int prc = ::poll(&pfd, 1, 55);
                if (prc < 0) break;
                if (prc == 0) {
                    // Gently decay level & bars if stream paused or silent
                    std::lock_guard<std::mutex> lk(mu_);
                    snap_.level *= 0.80f;
                    snap_.bass *= 0.78f;
                    snap_.treble *= 0.78f;
                    for (float& b : snap_.bands) b *= 0.80f;
                    continue;
                }
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    break;
                }
                if (pfd.revents & POLLIN) {
                    ssize_t n = ::read(fd, byte_buf.data() + leftover, byte_buf.size() - leftover);
                    if (n <= 0) break;
                    size_t total = leftover + static_cast<size_t>(n);
                    size_t complete_samples = total / sizeof(int16_t);
                    if (complete_samples > 0) {
                        std::memcpy(sample_buf.data(), byte_buf.data(), complete_samples * sizeof(int16_t));
                        process_pcm_samples(sample_buf.data(), complete_samples);
                    }
                    size_t consumed = complete_samples * sizeof(int16_t);
                    leftover = total - consumed;
                    if (leftover > 0) {
                        byte_buf[0] = byte_buf[consumed];
                    }
                }
            }

            ::close(fd);
            pid_t cur = child_pid_.exchange(-1);
            if (cur > 0) {
                ::kill(cur, SIGTERM);
                int st = 0;
                ::waitpid(cur, &st, 0);
            }

            if (!stop_requested_.load()) {
                // Retry once (or switch backend if parec wasn't installed)
                try_pw = !try_pw;
                std::this_thread::sleep_for(std::chrono::milliseconds(350));
            }
        }
    }
};
