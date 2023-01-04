#include <sys/sysinfo.h>
#include <sys/types.h>
#include <limits.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <istream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

struct Metric
{
    string Req, File;
    map<string, vector<float>> Data;
};
struct Jobstats
{
    unsigned long long Tick;
    unsigned int Rate;
    bool Rewrite;
    Metric Cpu, Mem, Read, Write;
    string Cgroup;
};

inline void get_data(struct Jobstats&);
inline void shrink_data(struct Jobstats&);
inline void write_output(struct Jobstats&);
unsigned long long get_uptime(void);
const vector<string> split_on_space(const string&);
const string file_to_string(const string&);
void rotate_output(string&);
template<typename T> void shrink_vector(vector<T> &);
void rewrite_tab(Metric&, unsigned long long int&);
void append_tab(Metric&, unsigned long long int&);

int main(int argc, char *argv[])
{
    struct Jobstats Job;
    // INITIALISE LOGGING
    string id = "-";
    Job.Cpu.Req = "1";
    string outputdir = "sps-local";
    if (argc == 5) // This is Slurm, override defaults
    {   // N.B Spank plugin can't use environment variables.
        id = string(argv[1]);         // JOBID
        Job.Cpu.Req = string(argv[2]);       // REQUESTED_CPUS
        string arrayjob = string(argv[3]);   // ARRAY_ID
        string arraytask = string(argv[4]);  // ARRAY_TASK
        if (arrayjob != "0")          // Is 0 when not array job
            id = arrayjob + "_" + arraytask;
        outputdir = "sps-" + id;
    }
    if (filesystem::exists(outputdir))
        rotate_output(outputdir);     // Up to 9 versions
    filesystem::create_directory(outputdir);
    string filestem = outputdir + "/" + outputdir;
    ofstream log;
    log.open(filestem + ".log");
    if (!log.is_open())
        exit(1); // Can't recover, can't log.
    ios_base::sync_with_stdio(false); // Don't need C compatibility, faster.
    // INITIALISE DATA
    Job.Rate = 1;
    Job.Mem.Req = file_to_string("/sys/fs/cgroup/memory/slurm/uid_" +
        to_string(getuid()) + "/job_" + id + "/memory.soft_limit_in_bytes");
    if (Job.Mem.Req == "") // Empty if not in a job
        Job.Mem.Req = "0";
    else
        Job.Mem.Req = to_string(stof(Job.Mem.Req)/1024/1024/1024); // Want GB, not B.
    Job.Read.Req = "0";
    Job.Write.Req = "0";
    Job.Cgroup = file_to_string("/proc/" + to_string(getpid()) + "/cgroup");
    Job.Cpu.File = filestem + "-cpu.tsv";
    Job.Mem.File = filestem + "-mem.tsv";
    Job.Read.File = filestem + "-read.tsv";
    Job.Write.File = filestem + "-write.tsv";
    // LOG STARTUP
    log << "CBB Profiling started ";
    time_t start = chrono::system_clock::to_time_t(chrono::system_clock::now());
    log << string(ctime(&start));
    log << "SLURM_JOB_ID\t\t" << id << endl;
    log << "REQ_CPU_CORES\t\t" << Job.Cpu.Req << endl;
    log << "REQ_MEMORY_GB\t\t" << Job.Mem.Req << endl;
    log << "Starting profiling...\n";
    log.flush();
    // READY TO GO
    try
    {
        if (daemon(1,0) == -1) // Don't chdir, do send output to /dev/null
            throw runtime_error("Failed to daemonise\n");
        // MAIN LOOP
        for (Job.Tick = 1; ; Job.Tick++) // Invariant: Tick = current iteration
        {
            get_data(Job);
            write_output(Job);
            if ((Job.Tick % 4096) == 0)
                shrink_data(Job); // RRD-like resizing to control size
            sleep(Job.Rate);
        }
        return 0;
        // END MAIN LOOP
    }
    catch (const exception &e)
    {
        log << e.what() << endl;
        log.flush();
        exit(1);
    }
} 

inline void get_data(struct Jobstats &Job)
{
    for (auto & [Comm, Vec] : Job.Cpu.Data) // Tick everything forward by one.
        Vec.push_back(0.0);                 // Then we can add the new values.
    for (auto & [Comm, Vec] : Job.Mem.Data)
        Vec.push_back(0.0);
    for (auto & [Comm, Vec] : Job.Read.Data)
        Vec.push_back(0.0);
    for (auto & [Comm, Vec] : Job.Write.Data)
        Vec.push_back(0.0);
    for (const auto & pd : filesystem::directory_iterator("/proc/"))
    {
        const auto full_path = pd.path();
        const string pid = full_path.filename();
        if (! all_of(pid.begin(), pid.end(), ::isdigit)) // Not a PID
           continue;
        string comm = "Unknown";      // These should always get overwritten
        float cpu, mem, read, write;  // or not used, but this way we can
        cpu = mem = read = write = 0; // tell if it ever goes wrong.
        try
        {
            const auto cgroup = file_to_string("/proc/" + pid + "/cgroup");
            if (cgroup != Job.Cgroup)
                continue; // We don't care
            string pid_root = "/proc/" + pid;
            comm = file_to_string(pid_root + "/comm");
            // /proc/PID/stat has lots of entries on a single line.
            const auto stats = split_on_space(file_to_string(pid_root + "/stat"));
            // The runtime of our pid is (system uptime) - (start time of PID)
            const float runtime = get_uptime() - stof(stats.at(21)); 
            // Total CPU time is the sum of user time and system time
            const float cputime = stof(stats.at(13)) + stof(stats.at(14));
            cpu = cputime / runtime; // Same as in "ps", NOT "top"
            mem = (stof(stats.at(23)) * 4) / 1024 / 1024; // 1 page = 4kb
            // /prod/PID/io has entries in the format "name: value".
            const auto iostats = split_on_space(file_to_string(pid_root + "/io"));
            read = stof(iostats.at(9)) / 1024 / 1024 / 1024; // Want GB
            write = stof(iostats.at(11)) / 1024 / 1024 / 1024;
        }
        catch (const exception &e)
        {
            continue; // A PID that's unreadable or stops existing.
        }
        if (!Job.Cpu.Data.count(comm)) // First time PID seen.
        {
            Job.Cpu.Data.emplace(comm, vector<float>(Job.Tick, 0.0));
            Job.Mem.Data.emplace(comm, vector<float>(Job.Tick, 0.0));
            Job.Read.Data.emplace(comm, vector<float>(Job.Tick, 0.0));
            Job.Write.Data.emplace(comm, vector<float>(Job.Tick, 0.0));
            Job.Rewrite = true; // Historical stats have changed
        }
        Job.Cpu.Data[comm].back() += cpu;     // Now, we just add new values
        Job.Mem.Data[comm].back() += mem;     // as we find them to get the
        Job.Read.Data[comm].back() += read;   // grand total.
        Job.Write.Data[comm].back() += write;
    }
}

inline void shrink_data(Jobstats &Job)
{
    if ((Job.Tick % 2) == 1) // MUST be even
        Job.Tick++;
    Job.Tick /= 2;
    for (auto & [pid, data] : Job.Cpu.Data)
        shrink_vector(data);
    for (auto & [pid, data] : Job.Mem.Data)
        shrink_vector(data);
    for (auto & [pid, data] : Job.Read.Data)
        shrink_vector(data);
    for (auto & [pid, data] : Job.Write.Data)
        shrink_vector(data);
    Job.Rate *= 2;
    Job.Rewrite = true; // History has changed. Full rewrite needed.
}

inline void write_output(struct Jobstats &Job)
{
    if (Job.Rewrite) // We have historical data to write
    {
        rewrite_tab(Job.Cpu, Job.Tick); 
        rewrite_tab(Job.Mem, Job.Tick); 
        rewrite_tab(Job.Read, Job.Tick); 
        rewrite_tab(Job.Write, Job.Tick); 
        Job.Rewrite = false;
    }
    else // Just need the latest data appending
    {
        append_tab(Job.Cpu, Job.Tick); 
        append_tab(Job.Mem, Job.Tick); 
        append_tab(Job.Read, Job.Tick); 
        append_tab(Job.Write, Job.Tick); 
    }
}

void rewrite_tab(Metric &met, unsigned long long int &current_tick) 
{
    if (filesystem::exists(met.File)) // Backup file, we may get killed
        filesystem::rename(met.File, met.File + ".bak");
    ofstream tab(met.File);
    if (!tab.is_open())
        throw runtime_error("Open of tab file failed\n");
    tab << "#TIME\tREQUESTED";
    for (const auto & [Comm, Vec] : met.Data)
        tab << "\t" << Comm;
    tab << "\n";
    for (unsigned long long int tick = 1; tick <= current_tick; tick++)
    {
        tab << tick << "\t" << met.Req;
        for (const auto & [Comm, Vec] : met.Data)
            tab << "\t" << Vec.at(tick - 1); // Vector is base 0
        tab << "\n";
    }
    tab.close();
    filesystem::remove(met.File + ".bak"); // No throw if not present
}

void append_tab(Metric &met, unsigned long long int &current_tick) 
{
    ofstream tab(met.File, ios::app);
    if (!tab.is_open())
        throw runtime_error("Open of tab file failed\n");
    tab << current_tick << "\t" << met.Req;
    for (const auto & [pid, value] : met.Data)
        tab << "\t" << value.back();
    tab << "\n";
    tab.close();
}

template<typename T> void shrink_vector(vector<T> &v)
{
    if ( (v.size() % 2) == 1) // MUST be even
        v.push_back(v.back());
    // Make the first half of the data into every other entry, so:
    // 1, 2, 3, 4, 5, 6 =>  2, 4, 6, 4, 5, 6
    for (size_t i = 0; i < v.size(); i++)
        if ( (i % 2) == 1 ) // Vectors are base 0 and we want the odd entries
            // In base 1, entry i => i/2, so 2 => 1, 4 => 2, 6 => 3, etc.
            // Base 0, that's:  i => ( (i+1) / 2 ) - 1
            v.at(((i+1)/2)-1) = v.at(i);
    v.resize(v.size()/2); // Throw away last half of the data.
}

unsigned long long get_uptime(void)
{
    struct sysinfo info;
    if (sysinfo(&info) == -1)
        throw runtime_error("Could not get sysinfo\n");
    return info.uptime * sysconf(_SC_CLK_TCK);
}
    
// Read the whole of a file into a single string, removing the final '\n'
const string file_to_string(const string &path)
{
    ifstream ifs(path);
    const string contents( (istreambuf_iterator<char>(ifs)),
        (istreambuf_iterator<char>()) );
    return(contents.substr(0,contents.length()-1));
}
 
// Get a file as a vector of strings, splits on any character in [:space:]
const vector<string> split_on_space(const string &path)
{
    istringstream iss(path);
    return vector<string>((istream_iterator<string>(iss)),
        istream_iterator<string>());
}

void rotate_output(string &s)
{
    for (int i = 1; i < 10; i++)
        if (!filesystem::exists(s + "." + to_string(i)))
            return filesystem::rename(s, s + "." + to_string(i));
    throw runtime_error("Exceed maximum rotate_output\n");
}
