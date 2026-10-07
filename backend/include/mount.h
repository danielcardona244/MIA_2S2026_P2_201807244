#ifndef MOUNT_H
#define MOUNT_H

#include <string>
#include <vector>
#include <map>
#include "structs.h"

using namespace std;

// estructura para guardar particiones montadas en RAM
struct MountEntry {
    string id;
    string path;
    string name;
};

// tabla de montajes en memoria
extern vector<MountEntry> mounted_list;

// comandos
void cmd_mount(map<string, string>& params);
void cmd_mounted();

// utilidad para buscar particion montada por id
bool get_mount_info(const string& id, string& out_path, string& out_name);

#endif
