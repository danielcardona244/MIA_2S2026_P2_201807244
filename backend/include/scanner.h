#ifndef SCANNER_H
#define SCANNER_H

#include <string>
#include <vector>
#include <map>

using namespace std;

// tokenizar respetando comillas
vector<string> tokenize(const string& input);

// separar clave=valor de un parametro
pair<string, string> split_param(const string& token);

// procesar una linea de comando
void process_line(const string& line);

// procesar un bloque de texto (script completo)
void process_input(const string& text);

#endif
