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
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

struct jobstats { ofstream log; bool full_logging; string outputdir;
                  string cpus; string id; string arrayjob; string arraytask;
                  string cgroup; string mem; string read; string write;
                  string filestem; string cpufile; string memfile;
                  string readfile; string writefile;
                  time_t start; unsigned long long tick; bool rewrite;
                  unsigned int samplerate;
		  map<string, string> pidcomm;
		  map<string, string> pidthreads;
                  map<string, vector<string>> pidfiles;
                  map<string, vector<string>> pidcpu;
                  map<string, vector<string>> pidmem;
                  map<string, vector<string>> pidread;
                  map<string, vector<string>> pidwrite; };
struct pidstats { string pid; string comm; string cpu; string mem;
                  string read; string write; string threads; };

inline const string get_env_var(const char * c)
    { const char *p = getenv(c); return(string(p ? p : "")); }
const unsigned long long get_uptime(void);
const vector<string> split_on_space(const string&);
const string file_to_string(const string&);
void rotate_output(string&);
void init_logging(int, char**, jobstats&);
void init_rc(struct jobstats&);
void init_data(struct jobstats&);
void log_startup(struct jobstats&);
void get_pid_data(const string&, pidstats&, const string&);
void emplace_new_pid(pidstats&, jobstats&);
void get_data(struct jobstats&);
void log_open_files(jobstats&, const string&);
void log_threads(jobstats&, const string&, const string&);
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
    init_logging(argc, argv, job);
    try // Now we can log the exceptions
    {
        init_rc(job);
        init_data(job);
        // Don't chdir to / but do send STDIN, STDOUT and STDERR to /dev/null
        if (daemon(1,0) == -1)
            throw runtime_error("Failed to daemonise\n");
        log_startup(job);
        // Invariant: job.tick = current iteration collecting and writing data
        for (job.tick = 1; ; job.tick++)
        {
            get_data(job);
            write_output(job);
            sleep(job.samplerate);
        }
        return 0;
    }
    catch (const exception &e)
    {
        job.log << e.what() << endl;
        job.log.flush();
        exit(1);
    }
} 

void rotate_output(string &s)
{
    for (int i = 1; i < 10; i++)
    {
        if (!filesystem::exists(s + "." + to_string(i)))
        {
            filesystem::rename(s, s + "." + to_string(i));
            return;
        }
    }
    throw runtime_error("Exceed maximum rotate_output\n");
}

void init_logging(int argc, char *argv[], jobstats &job)
{
    // We need a log file (a daemon can't output to STDOUT). To do that, we
    // need some key variables. This is a PITA. When the program is run by the
    // Slurm spank plugin the environment variables haven't been set yet
    // so we have to use the values we've been passed by it instead.
    // These are passed as JOBID REQUESTED_CPUS ARRAY_ID ARRAY_TASK.
    // N.B. argv[0] is the executable name.
    if (argc == 5)
    {
        job.id = string(argv[1]);
        job.cpus = string(argv[2]);
        job.arrayjob = string(argv[3]);
        job.arraytask = string(argv[4]);
        // SLURM_ARRAY_JOB_ID is always set to either the array job ID or 0 if
        // the job isn't an array. So, if it's non-zero, construct the full array
        // job ID.
        if (job.arrayjob != "0")
            job.id = job.arrayjob + "_" + job.arraytask;
        job.outputdir = "sps-" + job.id;
    }
    // Alternatively, we might be being run manually. Use defaults.
    else
    {
        job.id = "-";
        job.cpus = "1";
        job.outputdir = "sps-local";
    }
    // Now check whether the job output directory exists already. If it does,
    // rotate it out of the way (maximum 9 times).
    if (filesystem::exists(job.outputdir))
        rotate_output(job.outputdir);
    filesystem::create_directory(job.outputdir);
    job.filestem = job.outputdir + "/" + job.outputdir;
    // Ready. Start logging if we need to.
    job.log.open(job.filestem + ".log");
    if (!job.log.is_open())
        exit(1); // Can't recover, can't log.
    // Don't care about I/O compatibility with C. Let's go fast.
    ios_base::sync_with_stdio(false);
}

void init_rc(struct jobstats &job)
{
    // Check for rc file and set sample rate and full logging accordingly.
    job.full_logging = false;
    job.samplerate = 5;
    if (filesystem::exists("spsrc"))
    {
        // File layout is "OPTION VALUE" pairs, one pair per line.
        const auto rc = split_on_space(file_to_string("spsrc"));
        if (rc.empty())
            return;
        // Now iterate through the entries. This does, in fact, check all the
        // values as if they were options, but it doesn't make any functional
        // difference and allows us to recover from missing values.
        for (long unsigned int i = 0; i < rc.size(); i++)
        {
            string opt = rc.at(i);
            // If there's no next entry, we're done.
            if (i + 1 >= rc.size())
                return;
            string val = rc.at(i + 1);
            // Read options. Invalid options or values are silently ignored.
            if (opt == "SPS_FULL_LOGGING")
            {
                if (val == "true")
                    job.full_logging = true;
            }
            else if (opt == "SPS_SAMPLE_RATE")
            {
                if ( val != "" && all_of(val.begin(), val.end(), ::isdigit))
                {
                    int i = stoi(val);
                    if (i > 0 && i <= 60)
                        job.samplerate = i;
                }
            }
        }
    }
}

void init_data(struct jobstats &job)
{
    job.start = chrono::system_clock::to_time_t(chrono::system_clock::now());
    job.mem = file_to_string("/sys/fs/cgroup/memory/slurm/uid_" +
                           to_string(getuid()) + "/job_" + job.id +
                           "/memory.soft_limit_in_bytes");
    // If we're not in a job, that will have returned an empty string.
    if (job.mem == "")
        job.mem = "0";
    else
        job.mem = to_string(stof(job.mem)/1024); // RSS in in KB, not B.
    // We need values for "requested"
    job.read = "0";
    job.write = "0";
    // Everything is in a cgroup. If we're not in a job this is our login.
    job.cgroup = file_to_string("/proc/" + to_string(getpid()) + "/cgroup");
    job.cpufile = job.filestem + "-cpu.tsv";
    job.memfile = job.filestem + "-mem.tsv";
    job.readfile = job.filestem + "-read.tsv";
    job.writefile = job.filestem + "-write.tsv";
}

void log_startup(struct jobstats &job)
{
    job.log << "CBB Profiling started ";
    job.log << string(ctime(&job.start));
    job.log << "SLURM_JOB_ID\t\t" << job.id << endl;
    job.log << "REQ_CPU_CORES\t\t" << job.cpus << endl;
    job.log << "REQ_MEMORY_KB\t\t" << job.mem << endl;
    job.log << "CPU_OUT_FILE\t\t" << job.cpufile << endl;
    job.log << "MEM_OUT_FILE\t\t" << job.memfile << endl;
    job.log << "READ_OUT_FILE\t\t" << job.readfile << endl;
    job.log << "WRITE_OUT_FILE\t\t" << job.writefile << endl;
    job.log << "SPS_PROCESS\t\t" << to_string(getpid()) << endl;
    job.log << "SPS_SAMPLE_RATE\t\t" << job.samplerate
            << "s (faster events may not be detected)\n";
    if (job.full_logging)
        job.log << "SPS_FULL_LOGGING\ttrue\n";
    job.log << "Starting profiling...\n";
    job.log.flush();
}

void get_pid_data(const string& pid, struct pidstats &p, const string &pid_root)
{
    p.pid = pid;
    p.comm = file_to_string(pid_root + "/comm");
    // /proc/PID/stat has lots of entries on a single line. We get
    // them as a vector "stats" and then access them later via at().
    const auto stats = split_on_space(file_to_string(pid_root + "/stat"));
    // The runtime of our pid is (system uptime) - (start time of PID)
    const unsigned long long runtime = get_uptime() - 
                                       stoull(stats.at(21)); 
    // Total CPU time is the sum of user time and system time
    const unsigned long long cputime = stoull(stats.at(13)) +
                                       stoull(stats.at(14));
    p.threads = stats.at(19);
    // CPU is the number of whole CPUs being used and is calculated
    // using total used CPU time / total runtime.
    // This is the same as in "ps", NOT the same as "top".
    p.cpu = to_string(((float)cputime) / runtime);
    // RSS is in pages. 1 page = 4096 bytes, or 4 kb.
    p.mem = to_string(stol(stats.at(23)) * 4);
    // /prod/PID/io has entries in the format "name: value". We turn
    // these into a vector on space (including newline) so we can
    // access the values directly without further manipulation.
    const auto iostats = split_on_space(file_to_string(pid_root + "/io"));
    p.read = iostats.at(9);
    p.write = iostats.at(11);
}

void emplace_new_pid(pidstats &p, jobstats &job)
{
    job.pidcomm.emplace(p.pid, p.comm);
    if (job.full_logging)
        job.log << job.tick << ":" << job.pidcomm.at(p.pid) << "-"
                << p.pid << " started\n";
    job.pidcpu.emplace(p.pid, vector<string> {});
    job.pidmem.emplace(p.pid, vector<string> {});
    job.pidread.emplace(p.pid, vector<string> {});
    job.pidwrite.emplace(p.pid, vector<string> {});
    // Then, we need to pad the entry with 0's for every tick
    // that's already passed.
    for (unsigned long long i = 1; i < job.tick; i++)
    {
        job.pidcpu[p.pid].push_back("0");
        job.pidmem[p.pid].push_back("0");
        job.pidread[p.pid].push_back("0");
        job.pidwrite[p.pid].push_back("0");
    }
    // Now we're all initialised to add the new data, just like
    // any other PID. Set job.rewrite to "true" so we know to
    // rewrite our whole output later, adding a new column.
    job.rewrite = true;
}

void get_data(struct jobstats &job)
{
    // Iterate over the directories in /proc.
    for (const auto & pd : filesystem::directory_iterator("/proc/"))
    {
        const auto full_path = pd.path();
        const string pid = full_path.filename();
        // The PID directories are the ones with an all numeric name.
        if (! all_of(pid.begin(), pid.end(), ::isdigit))
           continue;
        // We only care about PIDs in the same cgroup as us.
        const auto pid_root = string(full_path);
        const auto cgroup = file_to_string(pid_root + "/cgroup");
        if (cgroup != job.cgroup)
           continue;
        // Reading the PID stats can go wrong in multple ways. Some PIDs
        // such as "sshd" and "slurmstepd" don't let us access their I/O
        // stats. Or, we could get part way through reading data, get
        // preempted, and when we resume the PID we're reading has now
        // terminated. The cleanest way to deal with this is to catch any
        // errors and then just skip those PIDs.
        struct pidstats p;
        try
        {
            get_pid_data(pid, p, pid_root);
        }
        catch (const exception &e)
        {
            continue;
        }
        // From this point on, exceptions are serious (e.g. memory errors)
        // so we stop trying to catch them.
        //
        // Now that we've got data, there a 3 scenarios for a PID:
        // 
        // 1. This is the first time we've ever seen it. We need to create
        //    new entries in the data maps.
        if (job.pidcomm.find(pid) == job.pidcomm.end())
            emplace_new_pid(p, job);
        // 2. This is an existing PID (or a PID that we've just initialised).
        //    We add our new values.
        job.pidcpu[pid].push_back(p.cpu);
        job.pidmem[pid].push_back(p.mem);
        job.pidread[pid].push_back(p.read);
        job.pidwrite[pid].push_back(p.write);
        // If full loggin is enabled, log some interesting stuff
        if (job.full_logging)
        {
            log_threads(job, pid, p.threads);
            log_open_files(job, pid);
        }
    }
    // 3. We have a PIDs that used to exist but has now exited. That means
    //    that the previous loop failed to get new values because there's
    //    no entries in /proc any more. We fix that by checking whether every
    //    vector in every map has the same number of data points as our tick.
    //    If they don't it's because it's not been updated yet, so we push a
    //    new blank data point onto the end.
    for (auto & [pid, data] : job.pidcpu)
        if (data.size() < job.tick)
            data.push_back("0"); 
    for (auto & [pid, data] : job.pidmem)
        if (data.size() < job.tick)
            data.push_back("0"); 
    for (auto & [pid, data] : job.pidread)
        if (data.size() < job.tick)
            data.push_back("0"); 
    for (auto & [pid, data] : job.pidwrite)
        if (data.size() < job.tick)
            data.push_back("0"); 
    // Now, we know for sure that every PID that's ever run has exactly the
    // same number of data points, which is the same as our current "tick".
}

void log_open_files(jobstats &job, const string &pid)
{
    for (const auto & p : filesystem::directory_iterator("/proc/" + pid + "/fd/"))
    {
        // If this is the first time we've seen the files for this PID,
        // initialise our data structure
        if (job.pidfiles.find(pid) == job.pidfiles.end())
            job.pidfiles.emplace(pid, vector<string> {});
        // The file descriptors are all links to real files. Read this.
        string link = string(p.path());
        char buff[PATH_MAX];
        ssize_t len = ::readlink(link.c_str(), buff, sizeof(buff)-1);
        // This could go wrong in multiple ways, including the process ending
        // before we can read the link. So if the read fails, just silently
        // abort.
        if (len == -1)
            continue;
        // If we got exactly PATH_MAX characters, we can't append a NULL. We
        // have to overwrite the last character.
        if (len == PATH_MAX)
            len -= 1;
        buff[len] = '\0';
        string file = string(buff);
        // We only want to log the first time we see a file handle.
        bool newfile = true;
        for (const auto f : job.pidfiles[pid]) 
            if (f == file)
                newfile = false;
        if (newfile)
        {
            // Add and log
            job.pidfiles[pid].push_back(file);
            job.log << job.tick << ":" << job.pidcomm[pid] << "-" << pid
                    << " " << file << endl;
            job.log.flush();
        }
    }
}

void log_threads(jobstats &job, const string &pid, const string &threads)
{
    // If the first time we've seen it, initialise.
    if (job.pidthreads.find(pid) == job.pidthreads.end())
        job.pidthreads.emplace(pid, string {});
    // If it's changed, update and log.
    if (threads != job.pidthreads.at(pid))
    {
        job.pidthreads.at(pid) = threads;
        job.log << job.tick << ":" << job.pidcomm.at(pid) << "-" << pid
                << " threads=" << threads << endl;
        job.log.flush();
    }
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
    if (filesystem::exists(job.readfile))
        filesystem::rename(job.readfile, job.readfile + ".bak");
    if (filesystem::exists(job.writefile))
        filesystem::rename(job.writefile, job.writefile + ".bak");
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
    if (filesystem::exists(job.readfile + ".bak"))
        filesystem::remove(job.readfile + ".bak");
    if (filesystem::exists(job.writefile + ".bak"))
        filesystem::remove(job.writefile + ".bak");
}

void rewrite_tab(string &tabfile, map<string, string> &pidcomm,
                 map<string, vector<string>> &data, string &req,
                 unsigned long long int &current_tick) 
{
        ofstream tab(tabfile);
        if (!tab.is_open())
            throw runtime_error("Open of CPU tab file failed\n");
        tab << "#TIME\tREQUESTED"; // Header
        for (const auto & [pid, comm] : pidcomm)
            tab << "\t" << comm << "-" << pid;
        tab << "\n";
        // Build the output by line.
        for (unsigned long long int tick = 1; tick <= current_tick; tick++)
        {
            tab << tick << "\t" << req;
            for (const auto & [pid, value] : data)
                tab << "\t" << data[pid].at(tick - 1); // Vector is base 0
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
    tab << current_tick << "\t" << req;
    for (const auto & [pid, value] : data)
        tab << "\t" << data[pid].at(current_tick - 1); // Vector is base 0
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
        rewrite_tab(job.readfile, job.pidcomm, job.pidread, job.read, job.tick); 
        rewrite_tab(job.writefile, job.pidcomm, job.pidwrite, job.write, job.tick); 
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
        append_tab(job.readfile, job.pidread, job.read, job.tick); 
        append_tab(job.writefile, job.pidwrite, job.write, job.tick); 
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
 
// Get a file as a vector of strings. Splits on any character in the [:space:]
// character set, which includes all horizontal and vertical padding
// including ' ', '\t' and '\n'.
const vector<string> split_on_space(const string &path)
{
    istringstream iss(path);
    const vector<string> results((istream_iterator<string>(iss)),
        istream_iterator<string>());
    return(results);
}
