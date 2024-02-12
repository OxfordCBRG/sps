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
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/program_options.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/support/date_time.hpp>

#ifdef HAVE_NVML
#include "nvidia_gpu.h"
#endif
#ifdef HAVE_RSMI
#include "amd_gpu.h"
#endif

namespace po = boost::program_options;
namespace logging = boost::log;
namespace src = boost::log::sources;
namespace keywords = boost::log::keywords;
namespace expr = boost::log::expressions;

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
    vector<Metric> GPU_load, GPU_mem, GPU_power;
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
void rewrite_tab(Metric&, unsigned long long int&, unsigned int&);
void append_tab(Metric&, unsigned long long int&, unsigned int&);

void init_log(string logname, bool console)
{
  logging::add_file_log
    (
     keywords::file_name = logname,
     keywords::format =
     (
      expr::stream
                << expr::format_date_time< boost::posix_time::ptime >("TimeStamp", "%Y-%m-%d %H:%M:%S")
                << ": <" << logging::trivial::severity
                << "> " << expr::smessage
      ),
     keywords::auto_flush = true);

  if (console) {
    logging::add_console_log
      (
       std::cout,
       keywords::format = ">> %Message%",
       keywords::auto_flush = true);
  }

  logging::core::get()->set_filter
    (
     logging::trivial::severity >= logging::trivial::info);
}


int main(int argc, char *argv[])
{
    struct Jobstats Job;
    string id = "-";
    string outputdir;
    string outfile;

    bool foreground = false;
    int jobid = -1;
    int ncpus = -1;
    int arrayid = -1;
    int arraytask = -1;
    string prefix = "";
    try {
      
      // command line options
      po::options_description desc("Slurm Profiling Service (sps) Options");
      desc.add_options()
	("help,h", "produce help message")
	("foreground,f", po::bool_switch(&foreground), "run in foreground")
	("job,j", po::value<int>(&jobid), "the job ID")
	("ncpus,c", po::value<int>(&ncpus), "the number of requested CPUs")
	("array-id,a", po::value<int>(&arrayid), "the array ID")
	("array-task,t", po::value<int>(&arraytask), "the array task")
	("prefix,p", po::value(&prefix), "prefix for output")
	;
      po::variables_map vm;
      po::store(po::parse_command_line(argc, argv, desc), vm);
      po::notify(vm);    

      if (vm.count("help")) {
	cout << desc << "\n";
	return 1;
      }
    }
    catch(exception& e) {
        cerr << e.what() << "\n";
    }

    if (jobid < 0) {
      const char *env_slurm_job_id = getenv("SLURM_JOB_ID");
      if (env_slurm_job_id)
	jobid = stoi(env_slurm_job_id);
    }
    if (ncpus < 0) {
      const char *env_slurm_num_cpus = getenv("SLURM_CPUS_ON_NODE");
      if (env_slurm_num_cpus)
	ncpus = stoi(env_slurm_num_cpus);
    }
    if (arrayid < 0) {
      const char *env_slurm_array_job_id = getenv("SLURM_ARRAY_JOB_ID");
      if (env_slurm_array_job_id)
	arrayid = stoi(env_slurm_array_job_id);
    }
    if (arraytask < 0) {
      const char *env_slurm_array_task_id = getenv("SLURM_ARRAY_TASK_ID");
      if (env_slurm_array_task_id)
	arraytask = stoi(env_slurm_array_task_id);
    }

    if (ncpus < 0)
      Job.Cpu.Req = "1";
    else
      Job.Cpu.Req = to_string(ncpus);

    if (arrayid > 0 && arraytask > 0)
      id = to_string(arrayid) + "_" + to_string(arraytask);
    else if (jobid > 0)
      id = to_string(jobid);

    if (prefix.length() > 0 && prefix.back() != '/')
      prefix.append("/");

    if (id != "-")
      outfile = "sps-" + id;
    else
      outfile = "sps-local";
    outputdir = prefix + outfile;

    try {
      if (filesystem::exists(outputdir))
        rotate_output(outputdir);     // Up to 9 versions
    }
    catch(exception& e) {
      cerr << e.what() << "\n";
      return 1;
    }

    filesystem::create_directory(outputdir);
    string filestem = outputdir + "/" + outfile;

    // setup logging
    init_log(filestem + ".log", foreground);
    logging::add_common_attributes();

    using namespace logging::trivial;
    //src::severity_logger< severity_level > lg;

    BOOST_LOG_TRIVIAL(info) << "SPS Profiling started";
    BOOST_LOG_TRIVIAL(info) << "SPS output directory: " << outputdir;
    BOOST_LOG_TRIVIAL(info) << "SLURM_JOB_ID: " << id;
    BOOST_LOG_TRIVIAL(info) << "REQ_CPU_CORES: " << Job.Cpu.Req;

    ios_base::sync_with_stdio(false); // Don't need C compatibility, faster.
    // INITIALISE DATA
    Job.Rate = 1;
    Job.Mem.Req = file_to_string("/sys/fs/cgroup/memory/slurm/uid_" +
        to_string(getuid()) + "/job_" + id + "/memory.soft_limit_in_bytes");
    if (Job.Mem.Req == "") // Empty if not in a job
        Job.Mem.Req = "0";
    else
      Job.Mem.Req = to_string(stof(Job.Mem.Req)/1024/1024/1024); // Want GB, not B.
    BOOST_LOG_TRIVIAL(info) << "REQ_MEMORY_GB: " << Job.Mem.Req;
    Job.Read.Req = "0";
    Job.Write.Req = "0";
    Job.Cgroup = file_to_string("/proc/" + to_string(getpid()) + "/cgroup");
    Job.Cpu.File = filestem + "-cpu.tsv";
    Job.Mem.File = filestem + "-mem.tsv";
    Job.Read.File = filestem + "-read.tsv";
    Job.Write.File = filestem + "-write.tsv";

    unsigned int num_nvidia_gpus=0;
    #ifdef HAVE_NVML
    NVML_RT_CALL(nvmlInit());
    NVML_RT_CALL(nvmlDeviceGetCount(&num_nvidia_gpus));
    #endif
    BOOST_LOG_TRIVIAL(info) << "number of NVIDIA GPUs: " << num_nvidia_gpus;

    uint32_t num_amd_gpus=0;
    #ifdef HAVE_RSMI
    RSMI_CALL(rsmi_init(0));
    RSMI_CALL(rsmi_num_monitor_devices(&num_amd_gpus));
    #endif
    BOOST_LOG_TRIVIAL(info) << "number of AMD GPUs: " << num_amd_gpus;

    Job.GPU_load.reserve(num_nvidia_gpus + num_amd_gpus);
    Job.GPU_mem.reserve(num_nvidia_gpus + num_amd_gpus);
    Job.GPU_power.reserve(num_nvidia_gpus + num_amd_gpus);

    #ifdef HAVE_NVML
    for (unsigned int i = 0; i < num_nvidia_gpus; i++)
    {
      nvmlDevice_t device;
      nvmlMemory_t device_memory;
      Metric gload, gmem, gpower;
      char deviceName[NVML_DEVICE_NAME_V2_BUFFER_SIZE];
      NVML_RT_CALL(nvmlDeviceGetHandleByIndex(i, &device));
      NVML_RT_CALL(nvmlDeviceGetMemoryInfo(device, &device_memory));
      NVML_RT_CALL(nvmlDeviceGetName(device, deviceName, NVML_DEVICE_NAME_V2_BUFFER_SIZE));
      BOOST_LOG_TRIVIAL(info) << "found NVIDIA GPU: " << deviceName;
      gload.File = filestem + "-gpu_load-" + to_string(i) + ".tsv";
      gload.Req = "0";
      gmem.File = filestem + "-gpu_mem-" + to_string(i) + ".tsv";
      gmem.Req = to_string(device_memory.total  / 1024 / 1024 / 1024); // Want GB
      gpower.File = filestem + "-gpu_power-" + to_string(i) + ".tsv";
      gpower.Req = "0";

      Job.GPU_load.push_back(gload);
      Job.GPU_mem.push_back(gmem);
      Job.GPU_power.push_back(gpower);
    }
    #endif
    #ifdef HAVE_RSMI
    for (uint32_t i = 0; i < num_amd_gpus; i++)
    {
      uint16_t device;
      uint64_t device_memory;
      Metric gload, gmem, gpower;
      const size_t deviceName_len = 60;
      char deviceName[deviceName_len];
      RSMI_CALL( rsmi_dev_id_get(i, &device) );
      RSMI_CALL( rsmi_dev_memory_total_get(i, RSMI_MEM_TYPE_VIS_VRAM, &device_memory) );
      RSMI_CALL( rsmi_dev_name_get(i, deviceName, deviceName_len) );
      BOOST_LOG_TRIVIAL(info) << "found AMD GPU: " << deviceName;
      gload.File = filestem + "-gpu_load-" + to_string(num_nvidia_gpus + i) + ".tsv";
      gload.Req = "0";
      gmem.File = filestem + "-gpu_mem-" + to_string(num_nvidia_gpus + i) + ".tsv";
      gmem.Req = to_string(device_memory  / 1024 / 1024 / 1024); // Want GB
      gpower.File = filestem + "-gpu_power-" + to_string(num_nvidia_gpus + i) + ".tsv";
      gpower.Req = "0";

      Job.GPU_load.push_back(gload);
      Job.GPU_mem.push_back(gmem);
      Job.GPU_power.push_back(gpower);
    }
    #endif
    // LOG STARTUP


    BOOST_LOG_TRIVIAL(info) << "Starting profiling...";

    // READY TO GO
    try
    {
      if (!foreground) {
	BOOST_LOG_TRIVIAL(info) << "daemonising sps";
	if (daemon(1,0) == -1) // Don't chdir, do send output to /dev/null
	  throw runtime_error("Failed to daemonise");
      }
      // MAIN LOOP
      for (Job.Tick = 1; ; Job.Tick++) // Invariant: Tick = current iteration
        {
            get_data(Job);
            write_output(Job);
            if ((Job.Tick % 4096) == 0)
                shrink_data(Job); // RRD-like resizing to control size
            sleep(Job.Rate);
        }
	#ifdef HAVE_NVML
	NVML_RT_CALL( nvmlShutdown( ) );
	#endif
	#ifdef HAVE_RSMI
	RSMI_CALL( rsmi_shut_down() );
	#endif
        return 0;
        // END MAIN LOOP
    }
    catch (const exception &e)
    {
      BOOST_LOG_TRIVIAL(error) << e.what();
#ifdef HAVE_NVML
      NVML_RT_CALL( nvmlShutdown( ) );
#endif
#ifdef HAVE_RSMI
      RSMI_CALL( rsmi_shut_down() );
#endif
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
    for (auto & Gpu: Job.GPU_load)
      for (auto & [Comm, Vec] : Gpu.Data)
	Vec.push_back(0.0);
    for (auto & Gpu: Job.GPU_mem)
      for (auto & [Comm, Vec] : Gpu.Data)
        Vec.push_back(0.0);
    for (auto & Gpu: Job.GPU_power)
      for (auto & [Comm, Vec] : Gpu.Data)
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
    unsigned int num_nvidia_gpus = 0;
    #ifdef HAVE_NVML
    // get GPU data
    NVML_RT_CALL(nvmlInit());
    NVML_RT_CALL(nvmlDeviceGetCount(&num_nvidia_gpus));
    for (unsigned int i = 0; i < num_nvidia_gpus; i++)
    {
      char serial[NVML_DEVICE_SERIAL_BUFFER_SIZE];
      nvmlDevice_t device;
      nvmlUtilization_st device_utilization;
      nvmlMemory_t device_memory;
      unsigned int power;
      NVML_RT_CALL(nvmlDeviceGetHandleByIndex(i, &device));
      NVML_RT_CALL(nvmlDeviceGetSerial(device, serial, NVML_DEVICE_SERIAL_BUFFER_SIZE));
      NVML_RT_CALL(nvmlDeviceGetUtilizationRates(device, &device_utilization));
      NVML_RT_CALL(nvmlDeviceGetMemoryInfo(device, &device_memory));
      NVML_RT_CALL(nvmlDeviceGetPowerUsage(device, &power));
      string comm = "total";

      if (!Job.GPU_load[i].Data.count(comm)) // First time GPU seen
        {

	  Job.GPU_load[i].Data.emplace(comm, vector<float>(Job.Tick, 0.0));
	  Job.GPU_mem[i].Data.emplace(comm, vector<float>(Job.Tick, 0.0));
	  Job.GPU_power[i].Data.emplace(comm, vector<float>(Job.Tick, 0.0));
	  Job.Rewrite = true;
	}

      Job.GPU_load[i].Data[comm].back() += device_utilization.gpu;
      Job.GPU_mem[i].Data[comm].back() += device_memory.used  / 1024 / 1024 / 1024; // Want GB
      Job.GPU_power[i].Data[comm].back() += static_cast<float>(power) / 1000.; // to convert to Watts

      // find programs running on GPU
      unsigned int info_count;
      nvmlReturn_t status;
      nvmlProcessInfo_t *infos;
      info_count = 0;
      status = nvmlDeviceGetComputeRunningProcesses(device, &info_count, infos);
      if (status == NVML_ERROR_INSUFFICIENT_SIZE) {
	info_count += 10;
	infos = new nvmlProcessInfo_t[info_count];
	NVML_RT_CALL(nvmlDeviceGetComputeRunningProcesses(device, &info_count, infos));
	for (unsigned int j=0; j<info_count; j++) {
	  string gpid_root = "/proc/" + to_string(infos[j].pid);
	  comm = file_to_string(gpid_root + "/comm");
	  if (!Job.GPU_mem[i].Data.count(comm)) // First time GPU process seen
	    {
	      Job.GPU_mem[i].Data.emplace(comm, vector<float>(Job.Tick, 0.0));
	      Job.Rewrite = true;
	    }
	  Job.GPU_mem[i].Data[comm].back() += infos[j].usedGpuMemory / 1024 / 1024 / 1024; // Want GB
	  delete[] infos;
	}
      }
    }
    #endif

    #ifdef HAVE_RSMI
    uint32_t num_amd_gpus;
    RSMI_CALL(rsmi_init(0));
    RSMI_CALL(rsmi_num_monitor_devices(&num_amd_gpus));
    for (uint32_t i = 0; i < num_amd_gpus; i++) {
      uint64_t power;
      uint64_t device_memory;
      uint32_t busy_percent;
      RSMI_CALL( rsmi_dev_power_ave_get(i, 0, &power) );
      RSMI_CALL( rsmi_dev_memory_usage_get(i, RSMI_MEM_TYPE_VIS_VRAM, &device_memory) );
      RSMI_CALL( rsmi_dev_busy_percent_get (i, &busy_percent) );
      string comm = "total";

      if (!Job.GPU_load[i].Data.count(comm)) // First time GPU seen
        {

	  Job.GPU_load[num_nvidia_gpus + i].Data.emplace(comm, vector<float>(Job.Tick, 0.0));
	  Job.GPU_mem[num_nvidia_gpus + i].Data.emplace(comm, vector<float>(Job.Tick, 0.0));
	  Job.GPU_power[num_nvidia_gpus + i].Data.emplace(comm, vector<float>(Job.Tick, 0.0));
	  Job.Rewrite = true;
	}

      Job.GPU_load[num_nvidia_gpus + i].Data[comm].back() += busy_percent;
      Job.GPU_mem[num_nvidia_gpus + i].Data[comm].back() += device_memory  / 1024 / 1024 / 1024; // Want GB
      Job.GPU_power[num_nvidia_gpus + i].Data[comm].back() += static_cast<float>(power) / 1000000.; // to convert to Watts
    }
    #endif
}

inline void shrink_data(Jobstats &Job)
{
  BOOST_LOG_TRIVIAL(info) << "shrink data";

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
    for (auto & Gpu: Job.GPU_load)
      for (auto & [pid, data] : Gpu.Data)
	shrink_vector(data);
    for (auto & Gpu: Job.GPU_mem)
      for (auto & [pid, data] : Gpu.Data)
        shrink_vector(data);
    for (auto & Gpu: Job.GPU_power)
      for (auto & [pid, data] : Gpu.Data)
        shrink_vector(data);
    Job.Rate *= 2;
    Job.Rewrite = true; // History has changed. Full rewrite needed.
}

inline void write_output(struct Jobstats &Job)
{
    if (Job.Rewrite) // We have historical data to write
    {
        rewrite_tab(Job.Cpu, Job.Tick, Job.Rate); 
        rewrite_tab(Job.Mem, Job.Tick, Job.Rate); 
        rewrite_tab(Job.Read, Job.Tick, Job.Rate); 
        rewrite_tab(Job.Write, Job.Tick, Job.Rate);
	for (auto & Gpu: Job.GPU_load)
	  rewrite_tab(Gpu, Job.Tick, Job.Rate);
	for (auto & Gpu: Job.GPU_mem)
	  rewrite_tab(Gpu, Job.Tick, Job.Rate);
	for (auto & Gpu: Job.GPU_power)
	  rewrite_tab(Gpu, Job.Tick, Job.Rate);
        Job.Rewrite = false;
    }
    else // Just need the latest data appending
    {
        append_tab(Job.Cpu, Job.Tick, Job.Rate); 
        append_tab(Job.Mem, Job.Tick, Job.Rate); 
        append_tab(Job.Read, Job.Tick, Job.Rate); 
        append_tab(Job.Write, Job.Tick, Job.Rate);
	for (auto & Gpu: Job.GPU_load)
	  append_tab(Gpu, Job.Tick, Job.Rate);
	for (auto & Gpu: Job.GPU_mem)
	  append_tab(Gpu, Job.Tick, Job.Rate);
	for (auto & Gpu: Job.GPU_power)
	  append_tab(Gpu, Job.Tick, Job.Rate);
    }
}

void rewrite_tab(Metric &met, unsigned long long int &current_tick, unsigned int &rate) 
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
        tab << tick * rate << "\t" << met.Req;
        for (const auto & [Comm, Vec] : met.Data)
            tab << "\t" << Vec.at(tick - 1); // Vector is base 0
        tab << "\n";
    }
    tab.close();
    filesystem::remove(met.File + ".bak"); // No throw if not present
}

void append_tab(Metric &met, unsigned long long int &current_tick, unsigned int &rate) 
{
    ofstream tab(met.File, ios::app);
    if (!tab.is_open())
        throw runtime_error("Open of tab file failed\n");
    tab << current_tick * rate << "\t" << met.Req;
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
