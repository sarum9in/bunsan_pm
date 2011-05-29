#include <iostream>
#include <exception>

#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
//#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/info_parser.hpp>

#include "repository.hpp"

#include "util.hpp"

int main(int argc, char **argv)
{
	std::string config_file;
	std::string extract_to;
	std::string package;
	try
	{
		//command line parse
		boost::program_options::options_description desc(argv[0]);
		desc.add_options()
			("help,h", "Print this information")
			("version,V", "Program version")
			("config,c", boost::program_options::value<std::string>(&config_file)->default_value("pm.rc"), "Configuration file")
			("clean", "Clean repository")
			("package,p", boost::program_options::value<std::string>(&package), "Package name")
			("extract,e", boost::program_options::value<std::string>(&extract_to), "Extract package to location");
		boost::program_options::variables_map vm;
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
		boost::program_options::notify(vm);
		if (vm.count("help"))
		{
			std::cerr<<desc<<std::endl;
			return 1;
		}
		if (vm.count("version"))
		{
			std::cerr<<"It is too early to announce project version"<<std::endl;
			return 1;
		}
		DLOG(config parse);
		boost::property_tree::ptree config;
		//boost::property_tree::xml_parser::read_xml(config_file, config);
		boost::property_tree::info_parser::read_info(config_file, config);
		//boost::property_tree::xml_parser::write_xml("/tmp/out", config);
		//return 0;
		bunsan::pm::repository repo(config.get_child("pm"));
		if (vm.count("clean"))
		{
			std::cerr<<"Attempt to clean repository"<<std::endl;
			repo.clean();
			return 0;
		}
		if (vm.count("package"))
		{
			if (vm.count("extract"))
			{//extracting
				std::cerr<<"Attempt to extract \""<<package<<"\" to \""<<extract_to<<"\""<<std::endl;
				repo.extract(package, extract_to);
			}
			else
			{//package info
				std::cerr<<"Package \""<<package<<"\""<<std::endl;
			}
		}
		//end parse
	}
	catch (std::exception &e)
	{
		DLOG(Oops! An exception has occured);
		std::cerr<<e.what()<<std::endl;
		return 200;
	}
}

