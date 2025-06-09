
#include "config.h"
#include "player.h"
#include <toml.hpp>

#include <iostream>
#include <sys/stat.h>

Config *config = new Config;

void config_parse() {
    char *string = getenv("HOME");
    std::string config_directory(string);
    config_directory += "/.config/lfp";
    mkdir(config_directory.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    
    std::string config_file(config_directory + "/lfp.toml");
    struct stat local_config_stat{};
    if (stat(config_file.c_str(), &local_config_stat) != 0) { // exists
        config_file = "/etc/lfp.toml";
    }
    
    try {
        auto c = toml::parse_file( config_file.c_str() );
        config->starting_tab_index = c["starting_tab_index"].value_or(config->starting_tab_index);
        config->volume = c["volume"].value_or(config->volume);
    } catch (...) {
    }
}

void config_load() {
    config_parse();
}

void config_save() {
    toml::table c;

    // Populate it with named integers
    c.insert("config_version", config->config_version);    
    c.insert("dpi_auto", config->dpi_auto);    
    c.insert("dpi", config->dpi);    
    c.insert("font", config->font);    
    c.insert("icons", config->icons);    
    
    c.insert("starting_tab_index", config->starting_tab_index);    
    c.insert("volume", player->volume_unthrottled);    
    
    std::string text;
    
    char *string = getenv("HOME");
    std::string config_directory(string);
    config_directory += "/.config/lfp";
    mkdir(config_directory.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    
    std::string config_file(config_directory + "/lfp.toml");
    
    std::ofstream file(config_file.c_str());
    file << c << "\n";
    file.close();
}