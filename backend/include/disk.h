#ifndef DISK_H
#define DISK_H

#include <string>
#include <map>
#include "structs.h"

using namespace std;

// lectura y escritura del MBR
MBR read_mbr(const string& path);
void write_mbr(const string& path, const MBR& mbr);

// comandos de administracion de discos
void cmd_mkdisk(map<string, string>& params);
void cmd_rmdisk(map<string, string>& params);
void cmd_fdisk(map<string, string>& params);

#endif
