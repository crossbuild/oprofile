#ifndef OPROF_START_UTIL_H
#define OPROF_START_UTIL_H

#include <cmath>
#include <string>
#include <iostream> 
 
inline double ratio(double x1, double x2)
{
	return fabs(((x1 - x2) / x2)) * 100;
}
 
std::string const get_user_filename(std::string const & filename); 
bool check_and_create_config_dir();
std::string const format(std::string const & orig, uint const maxlen);
int do_exec_command(std::string const & cmd);
std::string const do_open_file_or_dir(std::string const & base_dir, bool dir_only);
std::string const basename(std::string const & path_name);
int exec_command(std::string const & cmd_line, std::ostream & out, std::ostream & err);
int exec_command(std::string const & cmd_line, std::ostream & out);
 
#endif // OPROF_START_UTIL_H 
