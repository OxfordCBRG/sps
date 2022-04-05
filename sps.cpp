#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <istream>
#include <iterator>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

struct jobstats { ofstream log;
                  string cpus; string id; string arrayjob;
                  string arraytask; string cgroup; string mem;
                  string filestem; string cpufile; string memfile;
                  time_t start; unsigned long long tick; bool rewrite;
                  unsigned int samplerate;
                  map<string, string> pidcomm;
                  map<string, vector<string>> pidcpu;
                  map<string, vector<string>> pidmem; };

inline const string get_env_var(const char * c)
    { const char *p = getenv(c); return(string(p ? p : "0")); }
const unsigned long long get_uptime(void);
const vector<string> split_on_newline(const string&);
const string file_to_string(const string&);
void init_logging(struct jobstats&);
void init_data(struct jobstats&);
void get_data(struct jobstats&);
void write_output(struct jobstats&);
void backup_output(struct jobstats&);
void delete_backup(struct jobstats&);
void rewrite_tab(string&, map<string, string>&, map<string, vector<string>>&,
                 string&, unsigned long long int&);
void append_tab(string&, map<string, vector<string>>&, string&,
                unsigned long long int&);

int main(int argc, char *argv[])
{
    struct jobstats job;
    init_logging(job);     // As a deamon we can't log to STDOUT
    try {                  // Now we can log the exceptions
        init_data(job);
        job.log << "CBB Profiling started ";
        job.log << string(ctime(&job.start));
        job.log << "SLURM_JOB_ID\t" << job.id << endl;
        job.log << "REQ_CPU_CORES\t" << job.cpus << endl;
        job.log << "REQ_MEMORY_KB\t" << job.mem << endl;
        job.log << "SAMPLE_RATE\t" << job.samplerate << endl;
        job.log << "CPU_OUT_FILE\t" << job.cpufile << endl;
        job.log << "MEM_OUT_FILE\t" << job.memfile << endl;
        // Don't chdir to / but do send STDIN, STDOUT and STDERR to /dev/null
        if (daemon(1,0) == -1)
            throw runtime_error("Failed to daemonise\n");
        job.log << "SPS_PROCESS\t" << to_string(getpid()) << endl;
        job.log << "Starting profiling...\n";
        job.log.flush();
        // Invariant: job.tick = current iteration collecting and writing data
        for (job.tick = 1; ; job.tick++)
        {
            get_data(job);
            write_output(job);
            sleep(job.samplerate); // Is guaranteed to be an int
        }
        return 0;
    }
    catch (const exception &e)
    {
        job.log << e.what();
        job.log.flush();
        exit(1);
    }
} 

void init_logging(struct jobstats &job)
{
    // Get some key variables
    job.id = get_env_var("SLURM_JOB_ID");
    job.arrayjob = get_env_var("SLURM_ARRAY_JOB_ID");
    job.arraytask = get_env_var("SLURM_ARRAY_TASK_ID");
    // Reconstruct job.id if we're in an array
    if (job.arrayjob != "0")
        job.id = job.arrayjob + "_" + job.arraytask;
    // Use a default name stem if we didn't get a job ID
    job.filestem = job.id == "0" ? "sps" : (string("sps-" + job.id));
    // Ready
    job.log.open(job.filestem + ".log");
    if (!job.log.is_open())
        exit(1);
    // Don't care about I/O compatibility with C. Let's go fast.
    ios_base::sync_with_stdio(false);
}

void init_data(struct jobstats &job)
{
    job.start = chrono::system_clock::to_time_t(chrono::system_clock::now());
    job.cpus = get_env_var("SLURM_CPUS_ON_NODE");
    job.mem = file_to_string("/sys/fs/cgroup/memory/slurm/uid_" +
                           to_string(getuid()) + "/job_" + job.id +
                           "/memory.soft_limit_in_bytes");
    // If we're not in a job, that will have returned an empty string.
    job.mem = job.mem == "" ? string("0") : job.mem;
    if (job.mem != "0")
        // This is in bytes, RSS in in KB.
        job.mem = to_string(stof(job.mem)/1024);
    // Everything is in a cgroup. If we're not in a job this is our login.
    job.cgroup = file_to_string("/proc/" + to_string(getpid()) + "/cgroup");
    // Check for override of sample rate
    job.samplerate = stoi(get_env_var("SPS_SAMPLE_RATE")); // Will be an int
    job.samplerate = job.samplerate == 0 ? 1 : job.samplerate; 
    job.cpufile = job.filestem + "-cpu.tsv";
    job.memfile = job.filestem + "-mem.tsv";
}

void get_data(struct jobstats &job)
{
    // Iterate over the directories in /proc.
    for (const auto & p : filesystem::directory_iterator("/proc/"))
    {
        const auto full_path = p.path();
        const string pid = full_path.filename();
        const auto pid_root = string(full_path);
        const auto cgroup = file_to_string(pid_root + "/cgroup");
        // The PID directories are the ones with an all numeric name.
        // We only care about PIDs in the same cgroup as us.
        if (all_of(pid.begin(), pid.end(), ::isdigit) &&
           (cgroup == job.cgroup))
        {
            // /proc/PID/stat has lots of entries on a single line. We get
            // them as a vector "stats" and then access them later via at().
            const auto stats = split_on_newline(file_to_string(pid_root + "/stat"));
            // The runtime of our pid is (system uptime) - (start time of PID)
            const unsigned long long runtime = get_uptime() - 
                                               stoull(stats.at(21)); 
            // Total CPU time is the sum of user time and system time
            const unsigned long long cputime = stoull(stats.at(13)) +
                                               stoull(stats.at(14));
            // CPU is the number of whole CPUs being used and is calculated
            // using total used CPU time / total runtime.
            // This is the same as in "ps", NOT the same as "top".
            const auto cpu = ((float)cputime) / runtime;
            // RSS is in pages. 1 page = 4096 bytes, or 4 kb.
            const auto mem = stol(stats.at(23)) * 4;
            // There a 3 scenarios for a PID:
            // 
            // 1. This is the first time we've ever seen it. Firstly, we
            //    need to create new entries in the data maps.
            if (job.pidcomm.find(pid) == job.pidcomm.end())
            {
                job.pidcomm.emplace(pid, file_to_string(pid_root + "/comm"));
                job.pidcpu.emplace(pid, vector<string> {});
                job.pidmem.emplace(pid, vector<string> {});
                // Then, we need to pad the entry with 0's for every tick
                // that's already passed.
                for (unsigned long long i = 1; i < job.tick; i++)
                {
                    job.pidcpu[pid].push_back("0.0");
                    job.pidmem[pid].push_back("0");
                }
                // Now we're all initialised to add the new data, just like
                // any other PID. Set job.rewrite to "true" so we know to
                // rewrite our whole output later.
                job.rewrite = true;
            }
            // 2. This is an existing PID (or a PID that we've just
            //    initialised). We just add our new values.
            job.pidcpu[pid].push_back(to_string(cpu));
            job.pidmem[pid].push_back(to_string(mem));
        }
    }
    // 3. We have a PID that used to exist but has now exited. That means
    //    that the previous steps failed to get a new value because there's
    //    no entry in /proc any more. We fix that by checking whether every
    //    vector in every map has the same number of data points as our tick.
    //    If they don't it's because it's not been updated yet, so we push a
    //    new blank data point onto the end.
    for (auto & [pid, data] : job.pidcpu)
        if (data.size() < job.tick)
            data.push_back("0.0"); 
    for (auto & [pid, data] : job.pidmem)
        if (data.size() < job.tick)
            data.push_back("0"); 
    // Now, we know for sure that every PID that's ever run has exactly the
    // same number of data points, which is the same as our current "tick".
}

void backup_output(struct jobstats &job)
{
    // Because we can be terminated at any time, we need to make sure that
    // destructive file operations are atomic. So, we start by atomically
    // moving the current data to ".bak". We need to check it exists first,
    // because on the 1st tick it doesn't.
    if (filesystem::exists(job.cpufile))
        filesystem::rename(job.cpufile, job.cpufile + ".bak");
    if (filesystem::exists(job.memfile))
        filesystem::rename(job.memfile, job.memfile + ".bak");
}

void delete_backup(struct jobstats &job)
{
    // We atomically remove the ".bak" file, because we know that we've
    // finished outputting the new data. We still have to check that it
    // exists as before because of the 1st tick case.
    if (filesystem::exists(job.cpufile + ".bak"))
        filesystem::remove(job.cpufile + ".bak");
    if (filesystem::exists(job.memfile + ".bak"))
        filesystem::remove(job.memfile + ".bak");
}

void rewrite_tab(string &tabfile, map<string, string> &pidcomm,
                 map<string, vector<string>> &data, string &req,
                 unsigned long long int &current_tick) 
{
        ofstream tab(tabfile);
        if (!tab.is_open())
            throw runtime_error("Open of CPU tab file failed\n");
        tab << "#TIME\tREQUESTED\t"; // Header
        for (const auto & [pid, comm] : pidcomm)
            tab << comm << "-" << pid << "\t";
        tab << "\n";
        // Build the output by line.
        for (unsigned long long int tick = 1; tick <= current_tick; tick++)
        {
            tab << tick << "\t" << req << "\t";
            for (const auto & [pid, value] : data)
                tab << data[pid].at(tick - 1) + "\t"; // Vector is base 0
            tab << "\n";
        }
        tab.close();
}

void append_tab(string &tabfile, map<string, vector<string>> &data,
                string &req, unsigned long long int &current_tick) 
{
    ofstream tab(tabfile, ios::app);
    if (!tab.is_open())
        throw runtime_error("Open of CPU tab file failed\n");
    tab << current_tick << "\t" << req << "\t";
    for (const auto & [pid, value] : data)
        tab << data[pid].at(current_tick - 1) + "\t"; // Vector is base 0
    tab << "\n";
    tab.close();
}

void write_output(struct jobstats &job)
{
    // Output our CPU data. Note that (importantly) a C++ map is implemented
    // as a binary tree and is guaranteed to be ordered. Thus, because the
    // keys in each of pidcomm, pidcpu and pidmem are the same set of PIDs,
    // we can iterate over the maps and know that we'll get them in exactly
    // the same order each time. (This would not be true for a hash table,
    // which is an unordered_map in C++).
    //
    // There are two scenarios at this point:
    //
    // 1. If we have new PID data that needs backfilling to the start of
    //    logging then we need to perform a full rewrite of our stats to the
    //    output files. This is is indicated by setting job.rewrite to "true". 
    if (job.rewrite)
    {
        backup_output(job); // We could be killed whilst writing
        rewrite_tab(job.cpufile, job.pidcomm, job.pidcpu, job.cpus, job.tick); 
        rewrite_tab(job.memfile, job.pidcomm, job.pidmem, job.mem, job.tick); 
        delete_backup(job);      // Remove the copies
        job.rewrite = false; // Unset "rewrite" for the next pass
    }
    // 2. We don't. We can simply append the next tick of data to the end of
    //    the file. There is a small risk that we get killed in the middle of
    //    doing this, but it's a tiny operation that only runs once a second,
    //    and the worst it can do is add an incomplete last line. Making a
    //    backup would be almost as expensive as writing a large file from
    //    scratch, so defeats the point.
    else
    {
        append_tab(job.cpufile, job.pidcpu, job.cpus, job.tick); 
        append_tab(job.memfile, job.pidmem, job.mem, job.tick); 
    }
}

// We need to know how long the system has been up to calculate %CPU
const unsigned long long get_uptime(void)
{
    struct sysinfo info;
    if (sysinfo(&info) == -1)
        throw runtime_error("Could not get sysinfo\n");
    // For compatibility with the accounting statistics, we want this in
    // kernel ticks, so we multiply by the clock tick
    const unsigned long long uptime = info.uptime * sysconf(_SC_CLK_TCK);
    return(uptime);
}
    
// Read the whole of a file into a single string, removing the final '\n'
const string file_to_string(const string &path)
{
    ifstream ifs(path);
    const string contents( (istreambuf_iterator<char>(ifs)),
        (istreambuf_iterator<char>()) );
    return(contents.substr(0,contents.length()-1));
}
 
// Get a file as a vector of strings
const vector<string> split_on_newline(const string &path)
{
    istringstream iss(path);
    const vector<string> results((istream_iterator<string>(iss)),
        istream_iterator<string>());
    return(results);
}
