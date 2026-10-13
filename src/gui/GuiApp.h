// GuiApp.h
// The graphical interface (Dear ImGui). Like Cli, it contains no protocol code:
// every real action goes through MailService.
//
// Threading: network actions (login, check inbox, send) run on a background thread,
// so the window never freezes while waiting for the server. Only ONE job runs at a
// time, and while it runs the UI does not touch MailService; it only draws its own
// copies of the data. When the job finishes, its results are applied on the UI thread.
#pragma once

#include <functional>
#include <future>
#include <optional>
#include <string>
#include <vector>

#include "MailService.h"

class GuiApp {
public:
    explicit GuiApp(MailService& service);

    // Draw one frame. Called about 60 times per second by the main loop.
    void frame();

private:
    // ---- background jobs
    // A job's work runs on another thread and returns an "apply" function,
    // which is then run on the UI thread to update what the window shows.
    using Apply = std::function<void()>;
    struct Job {
        std::string label;           // shown while running, e.g. "Checking inbox..."
        std::future<Apply> result;
    };
    void startJob(const std::string& label, std::function<Apply()> work);
    void pollJob();
    bool busy() const { return job_.has_value(); }

    // ---- actions (each one starts a job)
    void doLogin();
    void doCheckInbox();
    void doSend();

    // ---- drawing
    void drawLogin();
    void drawMain();
    void drawToolbar();
    void drawInbox(float width, float height);
    void drawMessage(float width, float height);
    void drawCompose();
    void drawPasswordPrompt();
    void drawStatusBar();

    void setStatus(const std::string& text, bool error);
    void reloadLog();

    MailService& service_;
    std::optional<Job> job_;

    enum class Screen { Login, Main };
    Screen screen_ = Screen::Login;

    // Login form
    std::string loginEmail_;
    std::string loginPassword_;
    bool offerOffline_ = false;     // login failed because the server is unreachable

    // Data copied from MailService after each job (the UI only reads these)
    std::string accountEmail_;
    std::string accountServer_;
    std::vector<StoredMessage> messages_;
    int selected_ = -1;

    // Compose window
    bool composeOpen_ = false;
    std::string composeTo_;
    std::string composeSubject_;
    std::string composeBody_;

    // Password needed again (after "wrong password" during Check Inbox)
    bool askPassword_ = false;
    std::string retryPassword_;

    // Status bar and activity log
    std::string status_ = "Ready.";
    bool statusError_ = false;
    std::vector<std::string> logLines_;
};