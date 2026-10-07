#ifndef USERS_H
#define USERS_H

#include <map>
#include <string>

using namespace std;

struct SessionInfo {
    bool active = false;
    int uid = -1;
    int gid = -1;
    string user;
    string group;
    string id;
};

extern SessionInfo current_session;

void cmd_login(map<string, string>& params);
void cmd_logout();
void cmd_mkgrp(map<string, string>& params);
void cmd_rmgrp(map<string, string>& params);
void cmd_mkusr(map<string, string>& params);
void cmd_rmusr(map<string, string>& params);
void cmd_chgrp(map<string, string>& params);

#endif
