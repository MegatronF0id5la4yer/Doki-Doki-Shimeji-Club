#pragma once

#include <filesystem>
#include <functional>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <glib.h>
#include <nlohmann/json.hpp>
#include <string>
#include <unistd.h>

/**
 * Asynchronous newline-delimited JSON IPC bridge to Python natsuki_brain.py.
 * Runs all HTTP Gemini requests and local Vosk voice listening out-of-process
 * without ever blocking the Wayland GUI loop.
 */
class BrainClient {
public:
    using ReplyCallback = std::function<void(const nlohmann::json&)>;

    explicit BrainClient(std::string script_path) : script_path_(std::move(script_path)) {}

    ~BrainClient() { stop(); }

    BrainClient(const BrainClient&) = delete;
    BrainClient& operator=(const BrainClient&) = delete;

    bool start(ReplyCallback on_reply) {
        on_reply_ = std::move(on_reply);
        std::string python_bin = "python3";
        std::error_code ec;
        auto venv_py = std::filesystem::path(script_path_).parent_path().parent_path() / ".venv" / "bin" / "python3";
        if (std::filesystem::exists(venv_py, ec) && ::access(venv_py.c_str(), X_OK) == 0) {
            python_bin = venv_py.string();
        }
        gchar* argv[] = {
            const_cast<gchar*>(python_bin.c_str()),
            const_cast<gchar*>("-u"),
            const_cast<gchar*>(script_path_.c_str()),
            nullptr
        };
        gint in_fd = -1;
        gint out_fd = -1;
        GError* err = nullptr;

        if (!g_spawn_async_with_pipes(
                nullptr,
                argv,
                nullptr,
                static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD),
                nullptr,
                nullptr,
                &child_pid_,
                &in_fd,
                &out_fd,
                nullptr,
                &err)) {
            g_printerr("[brain] failed to launch python3: %s\n", err ? err->message : "unknown");
            if (err) {
                g_error_free(err);
            }
            return false;
        }

        stdin_stream_ = g_unix_output_stream_new(in_fd, TRUE);
        GInputStream* base_in = g_unix_input_stream_new(out_fd, TRUE);
        stdout_stream_ = g_data_input_stream_new(base_in);
        g_object_unref(base_in);

        cancellable_ = g_cancellable_new();
        running_ = true;
        schedule_read();
        return true;
    }

    int send(nlohmann::json payload) {
        if (!running_ || !stdin_stream_) {
            return -1;
        }
        int id = ++next_id_;
        payload["id"] = id;
        std::string line = payload.dump() + "\n";

        gsize written = 0;
        GError* err = nullptr;
        if (!g_output_stream_write_all(
                stdin_stream_, line.data(), line.size(), &written, nullptr, &err)) {
            if (err) {
                g_printerr("[brain] write error: %s\n", err->message);
                g_error_free(err);
            }
            return -1;
        }
        g_output_stream_flush(stdin_stream_, nullptr, nullptr);
        return id;
    }

    void stop() {
        if (!running_) {
            return;
        }
        running_ = false;
        if (stdin_stream_) {
            nlohmann::json bye = {{"cmd", "quit"}};
            std::string line = bye.dump() + "\n";
            g_output_stream_write_all(stdin_stream_, line.data(), line.size(), nullptr, nullptr, nullptr);
            g_output_stream_flush(stdin_stream_, nullptr, nullptr);
        }
        if (cancellable_) {
            g_cancellable_cancel(cancellable_);
            g_object_unref(cancellable_);
            cancellable_ = nullptr;
        }
        if (stdout_stream_) {
            g_object_unref(stdout_stream_);
            stdout_stream_ = nullptr;
        }
        if (stdin_stream_) {
            g_object_unref(stdin_stream_);
            stdin_stream_ = nullptr;
        }
        if (child_pid_ > 0) {
            g_spawn_close_pid(child_pid_);
            child_pid_ = 0;
        }
    }

private:
    void schedule_read() {
        if (!running_ || !stdout_stream_) {
            return;
        }
        g_data_input_stream_read_line_async(
            stdout_stream_,
            G_PRIORITY_DEFAULT,
            cancellable_,
            &BrainClient::on_line_ready,
            this
        );
    }

    static void on_line_ready(GObject* source, GAsyncResult* res, gpointer user_data) {
        auto* self = static_cast<BrainClient*>(user_data);
        if (!self->running_) {
            return;
        }
        GError* err = nullptr;
        gsize len = 0;
        gchar* raw = g_data_input_stream_read_line_finish(
            G_DATA_INPUT_STREAM(source), res, &len, &err);

        if (err) {
            if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                g_printerr("[brain] read error: %s\n", err->message);
            }
            g_error_free(err);
            return;
        }
        if (!raw) {
            self->running_ = false;
            return;
        }

        try {
            auto parsed = nlohmann::json::parse(raw);
            if (self->on_reply_) {
                self->on_reply_(parsed);
            }
        } catch (const std::exception& exc) {
            g_printerr("[brain] invalid JSON from python: %s (%s)\n", raw, exc.what());
        }
        g_free(raw);
        self->schedule_read();
    }

    std::string script_path_;
    GPid child_pid_ = 0;
    GOutputStream* stdin_stream_ = nullptr;
    GDataInputStream* stdout_stream_ = nullptr;
    GCancellable* cancellable_ = nullptr;
    bool running_ = false;
    int next_id_ = 0;
    ReplyCallback on_reply_;
};

