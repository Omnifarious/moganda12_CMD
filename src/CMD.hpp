
#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>

#include "./skylibs/split.hpp"
#include "./skylibs/eraseat.hpp"


namespace CMD {

using str = std::string;

typedef void (*ScriptFunction)(std::vector<str>&);
typedef void (*UpdateFunction)(void);
using Onzero = UpdateFunction;

typedef bool (*Condition)(std::vector<str>);
typedef void (*Result)(std::vector<str>);

str name = "CMD:\\";

str ver = "0.1.0";

str prgname;

str prompt;

std::ofstream logfile;

std::unordered_map<str, ScriptFunction> commands;

std::thread Engine;

std::thread Update;
std::thread Spawner;

inline str gettime(::std::time_t time) {
   // convert time to string
   char time_string_buffer[26];
   // This is called from multiple threads. It needs to use
   // asctime_r, not asctime.
   str temp = ::asctime_r(std::localtime(&time), time_string_buffer);
   temp.pop_back(); // remove trailing newline
   return temp;
}

inline str gettime() {
   std::time_t time = std::time(nullptr); // get current time
   return gettime(time);
}

void log(str message, str user = "game") {
   logfile << "[" << gettime() << "] <" << user << ">: " << message << '\n';
   logfile.flush();
}

void addcommand(str name, ScriptFunction func) {
   commands[name] = func;
}

void remcommand(str name) {
   auto const search_result = commands.find(name);
   if(search_result != commands.end()) {
      commands.erase(search_result);
   }
}

void bungle() {}

void execute_command(
        ScriptFunction command,
        ::std::vector <str> args)
{
   command(args);
}

void execute_update(UpdateFunction updater) {
   updater();
}

::std::mutex command_mutex;
::std::condition_variable command_ready_condition;
ScriptFunction command = nullptr;
::std::vector<str> arguments;

bool runcomm(str commandfull, Onzero onzero) {
   std::vector<str> args = sky::split(commandfull, " ");
   if(args.size() > 0) {
      str command_name = args[0];
      args.erase(args.begin());
      if (command_name == "exit") {
         // Treat exit specially.
         return false;
      }
      auto const search_result = commands.find(command_name);
      if (search_result != commands.end()) {
         ::std::unique_lock lk{command_mutex};
         CMD::log("Waiting for command to be empty.");
         command_ready_condition.wait(
                 lk,
                 []() { return command == nullptr; }
         );
         command = search_result->second;
         arguments.swap(args);
         lk.unlock();
         command_ready_condition.notify_all();
      } else {
         std::cout << "Command " + command_name + " not found.\n";
      }
   } else {onzero();}
   return true;
}

void engine_loop(
        ::std::stop_token const stop,
        str const name,
        UpdateFunction updater,
        ::std::chrono::microseconds update_interval
) {
   using clock = ::std::chrono::high_resolution_clock;
   auto const engine_start = clock::now();
   auto last_update = clock::now();
   auto next_update = last_update + update_interval;
   // Common code.
   auto check_time = [&last_update, &next_update, update_interval]() {
      auto current_time = clock::now();
      // Account for the clock possibly going backwards
      // (someone sets the time, for example).
      if (current_time < last_update) {
         last_update = current_time;
         next_update = current_time + update_interval;
      }
      return current_time;
   };
   while (!stop.stop_requested()) {
      auto current_time = check_time();
      auto time_to_wait = next_update - current_time;
      auto const time_zero = time_to_wait.zero();
      if (time_to_wait < time_zero) {
         time_to_wait = time_zero;
      }
      {
         ::std::ostringstream logmsg;
         auto const usecs = ::std::chrono::duration_cast<::std::chrono::microseconds>(time_to_wait);
         logmsg << "time_to_wait: " << usecs.count() << " usecs";
         CMD::log(logmsg.str());
      }
      { // Limit scrope for lock.
         ::std::unique_lock lk{command_mutex};
         CMD::log("Waiting for command to be non-empty or thread stop.");
         command_ready_condition.wait_for(
                 lk, time_to_wait,
                 [&stop] {
                    return stop.stop_requested() || command != nullptr;
                 }
         );
         if (!stop.stop_requested() && command != nullptr) {
            // Keep the lock and execute the command to keep the prompt from
            // printing while the command is executing.
            execute_command(command, arguments);
            // Now clear out the command and arguments to signal that execution
            // has completed.
            command = nullptr;
            arguments.clear();
            // Unlock the lock.
            lk.unlock();
            // Inform anybody waiting on the condition variable that the
            // condition they care about may have changed.
            command_ready_condition.notify_all();
         }
      }
      if (!stop.stop_requested()) {
         current_time = check_time();
         if (current_time >= next_update) {
            auto in_seconds = [](auto time) {
               using double_seconds = ::std::chrono::duration<double>;
               return double_seconds{time};
            };
            log("Running updater.");
            execute_update(updater);
            std::ostringstream logmsg;
            // Moving time forward in this way makes sure the update
            // interval remains consistent even if commands or
            // updates take some amount of time.
            last_update = next_update;
            next_update += update_interval;
            auto const last_since_start = in_seconds(last_update - engine_start);
            auto const next_since_start = in_seconds(next_update - engine_start);
            logmsg << "last_update: start+" << last_since_start.count() << "s next_update: start+" << next_since_start.count() << 's';
            CMD::log(logmsg.str());
         }
      }
   }
   log("Stop of engine loop requested!");
}

void errzero() {
   std::cout << "BAD BOY: ENETER A REAL COMMAND\n";
}

constexpr ::std::chrono::microseconds sixteenth_second{62500};

inline ::std::jthread init(
        str tename,
        str const &promptstyle,
        UpdateFunction updater = bungle,
        ::std::chrono::microseconds interval = sixteenth_second
) {
   logfile.open("log.txt", std::ios::app);
   prgname = tename;
   prompt = promptstyle;
   ::std::jthread game_thread{engine_loop, tename, updater, interval};
   logfile << "\n\n\n[" << gettime() << "] : " << name << " initialized\n" << name << " ver " << ver << '\n';
   return game_thread; // Named Value Return Optimization applies.
}

void command_loop(Onzero onzero) {
   bool exited = false;
   while(!exited) {
      str command;
      // This block will wait for any current command to run before printing
      // the prompt.
      {
         // Acquire the lock so we can look at the value of command.
         ::std::unique_lock lk{command_mutex};
         // Wait for any command in flight to be finished running.
         command_ready_condition.wait(
                 lk,
                 []() { return CMD::command == nullptr; }
         );
         // The lock will automatically be released when lk goes out of scope.
      }
      // Use ::std::flush to make sure prompt appears
      // before read.
      std::cout << prompt << ::std::flush;
      std::getline(std::cin, command);
      exited = !runcomm(command, onzero);
   }
}
} // end namespace CMD
