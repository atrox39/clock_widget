#include <cstdlib>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <cstdlib>

std::unordered_map<std::string, std::string> Dotenv(const std::string &archivo = ".env")
{
  std::unordered_map<std::string, std::string> env;
  std::ifstream file(archivo);
  std::string linea;

  while (std::getline(file, linea))
  {
    // Ignorar comentarios o líneas vacías
    if (linea.empty() || linea[0] == '#')
      continue;

    size_t pos = linea.find('=');
    if (pos != std::string::npos)
    {
      std::string clave = linea.substr(0, pos);
      std::string valor = linea.substr(pos + 1);

      // Limpiar espacios
      clave.erase(0, clave.find_first_not_of(" \t\r\n"));
      clave.erase(clave.find_last_not_of(" \t\r\n") + 1);
      valor.erase(0, valor.find_first_not_of(" \t\r\n"));
      valor.erase(valor.find_last_not_of(" \t\r\n") + 1);

      env[clave] = valor;

      // Establecer variable de entorno según el sistema operativo
      setenv(clave.c_str(), valor.c_str(), 1);
    }
  }

  return env;
}
