#pragma once

#include <string>
#include <cJSON.h>

class MusicCompanion {
public:
    static MusicCompanion& GetInstance() {
        static MusicCompanion instance;
        return instance;
    }
    void Start();
    std::string ListPlaylists();
    std::string PlayPlaylist(const std::string& name);
    std::string PlayerControl(const std::string& action);

private:
    void Run();
    std::string Request(const std::string& url, const std::string& body = "", size_t limit = 32768);
    std::string Execute(cJSON* command, double server_time);
    void Remind(const std::string& text, const std::string& audio, const std::string& base);
    std::string base_;
    unsigned last_command_ = 0;
    std::string last_result_ = "ok";
};
