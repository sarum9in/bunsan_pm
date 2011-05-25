#include "repository.hpp"

#include <stdexcept>
#include <string>
#include <set>
#include <map>
#include <deque>
#include <queue>
#include <vector>
#include <memory>
#include <mutex>

#include <cstdlib>
#include <cassert>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/filesystem.hpp>

#include "util.hpp"

its::repository::repository(const boost::property_tree::ptree &config_): config(config_)
{
	DLOG(creating repository instance);
	flock.reset(new boost::interprocess::file_lock(config.get<std::string>("lock.global").c_str()));
}

void check_package_name(const std::string &package)
{
	if (!boost::algorithm::all(package, [](char c){return c=='_' || ('0'<=c && c<='9') || ('a'<=c && c<='z') || ('A'<=c || c<='Z');}))
		throw std::runtime_error("illegal package name \""+package+"\"");
}

void its::repository::extract(const std::string &package, const boost::filesystem::path &destination)
{
	//boost::interprocess::sharable_lock<boost::interprocess::file_lock> lk(flock);
	check_package_name(package);
	SLOG("extract \""<<package<<"\" to \""<<destination<<"\"");
	boost::interprocess::scoped_lock<boost::interprocess::file_lock> lk(*flock);
	boost::interprocess::scoped_lock<std::mutex> lk2(slock);
	DLOG(trying to update);
	update(package);
	native ntv(&config);
	DLOG(trying to extract);
	ntv.extract(package, destination);
}

void its::repository::clean()
{
	DLOG(trying to clean cache);
	boost::interprocess::scoped_lock<boost::interprocess::file_lock> lk(*flock);
	boost::interprocess::scoped_lock<std::mutex> lk2(slock);
	native ntv(&config);
	ntv.clean();
}

void its::repository::update(const std::string &package)
{
	SLOG("updating \""<<package<<"\"");
	check_dirs();
	check_cycle(package);
	std::map<std::string, std::shared_future<bool>> status;
	std::mutex lock;
	DLOG(starting parallel build);
	dfs(package, status, lock);
}

bool its::repository::dfs(const std::string &package, std::map<std::string, std::shared_future<bool>> &status, std::mutex &lock)
{
	native ntv(&config);
	std::vector<std::string> deps = ntv.depends(package);
	for (auto i = deps.cbegin(); i!=deps.cend(); ++i)
	{
		std::unique_lock<std::mutex> lk(lock);
		if (status.find(*i)==status.end())
			status[*i] = std::async(&its::repository::dfs, this, *i, std::ref(status), std::ref(lock));
	}
	bool updated = false;
	for (auto i = deps.cbegin(); i!=deps.cend(); ++i)
	{
		std::shared_future<bool> future;
		{
			std::unique_lock<std::mutex> lk(lock);
			future = status.at(*i);
		}
		if (future.get())
			updated = true;
	}
	SLOG("updated=\""<<updated<<"\"");
	SLOG("starting updating \""<<package<<"\"");
	if (ntv.source_outdated(package))
	{
		updated = true;
		ntv.fetch(package);
	}
	if (updated || ntv.package_outdated(package))
	{
		updated = true;
		ntv.build(package);
	}
	SLOG("\""<<package<<"\" was "<<(updated?"updated":"not updated"));
	return updated;
}

void check_dir(const boost::filesystem::path &dir)
{
	SLOG("checking "<<dir);
	if (!boost::filesystem::is_directory(dir))
	{
		if (!boost::filesystem::exists(dir))
		{
			SLOG("directory "<<dir<<" was not found");
		}
		else
		{
			SLOG(dir<<" is not a directory: starting recursive remove");
			boost::filesystem::remove_all(dir);
		}
		SLOG("trying to create "<<dir);
		boost::filesystem::create_directory(dir);
		DLOG(created);
	}
}

void its::repository::check_dirs()
{
	DLOG(checking directories);
	check_dir(config.get<std::string>("dir.source"));
	check_dir(config.get<std::string>("dir.package"));
	check_dir(config.get<std::string>("dir.tmp"));
}

enum class state{out, in, visited};
void check_cycle(const std::string &package, std::map<std::string, state> &status, std::function<std::vector<std::string> (const std::string &)> dget)
{
	if (status.find(package)==status.end())
		status[package] = state::out;
	std::vector<std::string> deps = dget(package);
	switch (status[package])
	{
	case state::out:
		status[package] = state::in;
		for (auto i = deps.cbegin(); i!=deps.cend(); ++i)
		{
			check_cycle(*i, status, dget);
		}
		break;
	case state::in:
		throw std::runtime_error("cycle dependencies in \""+package+"\"");
		break;
	case state::visited:
		break;
	default:
		assert(false);
	}
	status[package] = state::visited;
}

void its::repository::check_cycle(const std::string &package)
{
	SLOG("trying to find circular dependencies starting with \""<<package<<"\"");
	std::map<std::string, state> status;
	native ntv(&config);
	::check_cycle(package, status, std::bind(&its::repository::native::depends, &ntv, std::placeholders::_1));
	DLOG((circular dependencies was not found, that is good!));
}

std::mutex its::repository::slock;

