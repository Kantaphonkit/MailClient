// GuiApp.cpp
#include "GuiApp.h"

#include <chrono>
#include <deque>
#include <exception>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "imgui.h"
#include "imgui_stdlib.h"  // ImGui::InputText for std::string

#include "Account.h"
#include "Logger.h"
#include "ProtocolErrors.h"
#include "TcpConnection.h"

namespace {

    const ImVec4 kErrorColor(0.80f, 0.16f, 0.12f, 1.0f);
    const ImVec4 kOkColor(0.11f, 0.48f, 0.29f, 1.0f);
    const ImVec4 kMutedColor(0.42f, 0.45f, 0.50f, 1.0f);

    std::string trim(const std::string& s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    // The same error messages as the CLI, so both interfaces explain problems the same way.
    std::string describe(const std::exception_ptr& error) {
        try {
            std::rethrow_exception(error);
        }
        catch (const NetworkError& e) {
            return std::string("Cannot reach the mail server: ") + e.what();
        }
        catch (const AuthError& e) {
            return e.what();
        }
        catch (const ProtocolError& e) {
            return std::string("The server refused the request: ") + e.what();
        }
        catch (const std::invalid_argument& e) {
            return std::string("Invalid input: ") + e.what();
        }
        catch (const std::exception& e) {
            Logger::error(std::string("Unexpected error: ") + e.what());
            return std::string("Error: ") + e.what();
        }
    }

    std::string orDefault(const std::string& s, const char* fallback) { return s.empty() ? fallback : s; }

}  // namespace

GuiApp::GuiApp(MailService& service) : service_(service) {
    loginEmail_ = service_.config().defaultEmail;
    reloadLog();
}

// ------------------------------------------------------------------ background jobs

void GuiApp::startJob(const std::string& label, std::function<Apply()> work) {
    if (busy()) return;  // only one job at a time
    setStatus(label, false);
    // std::async runs `work` on a new thread. Any exception is caught there and turned
    // into an Apply that shows the error, so a failed job can never crash the window.
    job_ = Job{ label, std::async(std::launch::async, [this, work]() -> Apply {
        try {
            return work();
        }
 catch (...) {
  std::string text = describe(std::current_exception());
  return [this, text] { setStatus(text, true); };
}
}) };
}

void GuiApp::pollJob() {
    if (!job_) return;
    using namespace std::chrono_literals;
    if (job_->result.wait_for(0ms) != std::future_status::ready) return;  // still running
    Apply apply = job_->result.get();
    job_.reset();   // the worker is finished: the UI may use MailService again
    apply();        // update the window with the results (on the UI thread)
    reloadLog();
}

// ------------------------------------------------------------------ actions

void GuiApp::doLogin() {
    std::string email = trim(loginEmail_);
    std::string password = loginPassword_;
    if (password.empty()) {
        setStatus("Please enter the password.", true);
        return;
    }
    Account acc;
    try {
        acc = Account::forEmail(email, service_.config());
    }
    catch (const std::invalid_argument& e) {
        setStatus(e.what(), true);
        return;
    }
    acc.password = password;
    offerOffline_ = false;

    startJob("Checking login with " + acc.server + "...", [this, acc]() -> Apply {
        service_.login(acc);
        try {
            service_.verifyLogin();
        }
        catch (const AuthError& e) {
            service_.logout();
            std::string text = e.what();
            return [this, text] { setStatus(text, true); };
        }
        catch (const NetworkError& e) {
            // Keep the account selected: the user may continue offline.
            std::string text = std::string("Cannot reach the mail server: ") + e.what();
            return [this, text] {
                offerOffline_ = true;
                setStatus(text, true);
                };
        }
        std::vector<StoredMessage> local = service_.localMessages();
        return [this, acc, local] {
            accountEmail_ = acc.email;
            accountServer_ = acc.server;
            messages_ = local;
            selected_ = -1;
            loginPassword_.clear();
            screen_ = Screen::Main;
            setStatus("Logged in as " + acc.email + ".", false);
            };
        });
}

void GuiApp::doCheckInbox() {
    if (!service_.hasPassword()) {  // e.g. offline login, or the password was wrong
        askPassword_ = true;
        return;
    }
    startJob("Checking inbox...", [this]() -> Apply {
        try {
            InboxResult r = service_.checkInbox();
            std::vector<StoredMessage> local = service_.localMessages();
            return [this, r, local] {
                messages_ = local;
                if (selected_ >= static_cast<int>(messages_.size())) selected_ = -1;
                setStatus("Server has " + std::to_string(r.onServer) + " message(s), " +
                    std::to_string(r.newSaved) + " new. (" + std::to_string(r.millis) +
                    " ms)",
                    false);
                };
        }
        catch (const AuthError& e) {
            std::string text = std::string(e.what()) + " Please enter the password again.";
            return [this, text] {
                setStatus(text, true);
                askPassword_ = true;
                };
        }
        });
}

void GuiApp::doSend() {
    MailMessage msg;
    std::stringstream parts(composeTo_);
    std::string part;
    while (std::getline(parts, part, ',')) {
        std::string addr = trim(part);
        if (addr.empty()) continue;
        if (!MailMessage::isValidAddress(addr)) {
            setStatus("'" + addr + "' is not a valid email address.", true);
            return;
        }
        msg.to.push_back(addr);
    }
    if (msg.to.empty()) {
        setStatus("Please enter at least one recipient.", true);
        return;
    }
    msg.subject = composeSubject_;
    msg.body = composeBody_;

    startJob("Sending message...", [this, msg]() -> Apply {
        long long ms = service_.send(msg);
        size_t count = msg.to.size();
        return [this, ms, count] {
            composeOpen_ = false;
            composeTo_.clear();
            composeSubject_.clear();
            composeBody_.clear();
            setStatus("Message sent to " + std::to_string(count) + " recipient(s). (" +
                std::to_string(ms) + " ms)",
                false);
            };
        });
}

// ------------------------------------------------------------------ helpers

void GuiApp::setStatus(const std::string& text, bool error) {
    status_ = text;
    statusError_ = error;
}

// Show the last lines of the log file in the "Activity log" panel.
void GuiApp::reloadLog() {
    std::ifstream in(service_.config().logFile);
    std::deque<std::string> last;
    std::string line;
    while (std::getline(in, line)) {
        last.push_back(line);
        if (last.size() > 200) last.pop_front();
    }
    logLines_.assign(last.begin(), last.end());
}

// ------------------------------------------------------------------ drawing

void GuiApp::frame() {
    pollJob();

    // One window that always fills the whole application window.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##root", nullptr, flags);
    if (screen_ == Screen::Login) {
        drawLogin();
    }
    else {
        drawMain();
    }
    ImGui::End();

    if (composeOpen_) drawCompose();
    drawPasswordPrompt();
}

void GuiApp::drawLogin() {
    const float formWidth = 420.0f;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPos(ImVec2((avail.x - formWidth) * 0.5f, avail.y * 0.22f));
    ImGui::BeginChild("##login", ImVec2(formWidth, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ImGui::TextUnformatted("Lightweight Mail Client");
    ImGui::TextColored(kMutedColor, "Server: %s  (SMTP %d, POP3 %d)", service_.config().server.c_str(),
        service_.config().smtpPort, service_.config().pop3Port);
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginDisabled(busy());
    ImGui::TextUnformatted("Email");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##email", &loginEmail_);
    ImGui::TextUnformatted("Password");
    ImGui::SetNextItemWidth(-1);
    bool enter = ImGui::InputText("##password", &loginPassword_,
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();
    if (ImGui::Button("Log in", ImVec2(-1, 0)) || enter) doLogin();

    if (offerOffline_) {
        ImGui::Spacing();
        ImGui::TextWrapped("The server is not reachable. You can still read the messages saved on this computer.");
        if (ImGui::Button("Continue offline", ImVec2(-1, 0))) {
            accountEmail_ = service_.account().email;
            accountServer_ = service_.account().server;
            messages_ = service_.localMessages();
            selected_ = -1;
            loginPassword_.clear();
            offerOffline_ = false;
            screen_ = Screen::Main;
            setStatus("Offline: showing saved messages. Check Inbox tries the server again.", false);
        }
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (busy()) {
        ImGui::TextColored(kMutedColor, "%s", job_->label.c_str());
    }
    else if (statusError_) {
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

void GuiApp::drawMain() {
    drawToolbar();
    ImGui::Separator();

    const float statusHeight = ImGui::GetFrameHeightWithSpacing() * 1.2f;
    const float logHeight = 130.0f;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float listHeight = avail.y - statusHeight - logHeight - ImGui::GetStyle().ItemSpacing.y * 2;
    float listWidth = avail.x * 0.48f;

    drawInbox(listWidth, listHeight);
    ImGui::SameLine();
    drawMessage(0, listHeight);

    // Activity log: the newest lines of logs/client.log
    ImGui::BeginChild("##log", ImVec2(0, logHeight), ImGuiChildFlags_Borders);
    ImGui::TextColored(kMutedColor, "Activity log (%s)", service_.config().logFile.c_str());
    for (const std::string& line : logLines_) {
        bool isError = line.find("[ERROR]") != std::string::npos;
        if (isError) ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        ImGui::TextUnformatted(line.c_str());
        if (isError) ImGui::PopStyleColor();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1) ImGui::SetScrollHereY(1.0f);  // stay at the bottom
    ImGui::EndChild();

    drawStatusBar();
}

void GuiApp::drawToolbar() {
    ImGui::BeginDisabled(busy());
    if (ImGui::Button("Check Inbox")) doCheckInbox();
    ImGui::SameLine();
    if (ImGui::Button("Compose")) composeOpen_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Switch account")) {
        service_.logout();  // safe: no job is running (buttons are disabled while busy)
        messages_.clear();
        selected_ = -1;
        composeOpen_ = false;
        loginEmail_ = accountEmail_;
        screen_ = Screen::Login;
        setStatus("Logged out.", false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(kMutedColor, "  %s  @  %s", accountEmail_.c_str(), accountServer_.c_str());
}

void GuiApp::drawInbox(float width, float height) {
    ImGui::BeginChild("##inbox", ImVec2(width, height), ImGuiChildFlags_Borders);
    ImGui::Text("Inbox: %d message(s)", static_cast<int>(messages_.size()));
    if (messages_.empty()) {
        ImGui::TextColored(kMutedColor, "No messages yet. Click \"Check Inbox\".");
    }
    else if (ImGui::BeginTable("##list", 3,
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Subject", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(messages_.size()); ++i) {
            const MailMessage& m = messages_[i].message;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            // One selectable spanning all columns makes the whole row clickable.
            if (ImGui::Selectable(orDefault(m.from, "(unknown)").c_str(), selected_ == i,
                ImGuiSelectableFlags_SpanAllColumns)) {
                selected_ = i;
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(orDefault(m.subject, "(no subject)").c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.date.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void GuiApp::drawMessage(float width, float height) {
    ImGui::BeginChild("##message", ImVec2(width, height), ImGuiChildFlags_Borders);
    if (selected_ < 0 || selected_ >= static_cast<int>(messages_.size())) {
        ImGui::TextColored(kMutedColor, "Select a message on the left to read it.");
    }
    else {
        const StoredMessage& sm = messages_[selected_];
        const MailMessage& m = sm.message;
        std::string to;
        for (size_t i = 0; i < m.to.size(); ++i) to += (i ? ", " : "") + m.to[i];

        ImGui::Text("Subject: %s", orDefault(m.subject, "(no subject)").c_str());
        ImGui::Text("From:    %s", orDefault(m.from, "(unknown)").c_str());
        ImGui::Text("To:      %s", to.c_str());
        ImGui::Text("Date:    %s", m.date.c_str());
        ImGui::TextColored(kMutedColor, "Stored as %s", sm.fileName.c_str());
        ImGui::Separator();
        // Read-only text box: the body can be selected and copied, but not changed.
        std::string body = m.body;
        ImGui::InputTextMultiline("##body", &body, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::EndChild();
}

void GuiApp::drawCompose() {
    ImGui::SetNextWindowSize(ImVec2(560, 440), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Compose", &composeOpen_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::BeginDisabled(busy());
    ImGui::TextColored(kMutedColor, "From: %s", accountEmail_.c_str());
    ImGui::TextUnformatted("To (separate several addresses with commas)");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##to", &composeTo_);
    ImGui::TextUnformatted("Subject");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##subject", &composeSubject_);
    ImGui::TextUnformatted("Message");
    float buttonRow = ImGui::GetFrameHeightWithSpacing() + 4;
    ImGui::InputTextMultiline("##body", &composeBody_, ImVec2(-1, -buttonRow));
    if (ImGui::Button("Send")) doSend();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) composeOpen_ = false;
    ImGui::EndDisabled();
    if (busy()) {
        ImGui::SameLine();
        ImGui::TextColored(kMutedColor, "%s", job_->label.c_str());
    }
    ImGui::End();
}

void GuiApp::drawPasswordPrompt() {
    if (askPassword_) {
        ImGui::OpenPopup("Password needed");
        askPassword_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(380, 0));
    if (ImGui::BeginPopupModal("Password needed", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("Enter the password for %s.", accountEmail_.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("##retry", &retryPassword_,
            ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ImGui::Button("OK") || enter) && !retryPassword_.empty()) {
            service_.setPassword(retryPassword_);  // safe: no job is running while this is open
            retryPassword_.clear();
            ImGui::CloseCurrentPopup();
            doCheckInbox();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            retryPassword_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void GuiApp::drawStatusBar() {
    ImGui::Separator();
    if (busy()) {
        // A small animated dot pattern so the user sees that work is going on.
        int dots = static_cast<int>(ImGui::GetTime() * 3.0) % 4;
        ImGui::TextColored(kMutedColor, "%s%s", job_->label.c_str(), std::string(dots, '.').c_str());
    }
    else {
        ImGui::TextColored(statusError_ ? kErrorColor : kOkColor, "%s", status_.c_str());
    }
}