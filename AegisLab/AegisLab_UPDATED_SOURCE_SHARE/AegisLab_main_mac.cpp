#include <unordered_map>
#include <iostream>

#include <vector>

#include <string>

#include <fstream>

#include <filesystem>

#include <iomanip>

#include <cctype>

#include <cstdint>

#include <chrono>

#include <thread>

#include <sstream>

#include <map>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>




#include <libproc.h>

#include <sys/proc_info.h>

#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/fat.h>
#include <mach/machine.h>
#include <mach/vm_prot.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/error.h>



struct Region

{

    uint64_t start = 0;

    uint64_t end = 0;

    uint64_t size = 0;



    std::string protection;

    std::string path;

};



struct ComparisonStats

{

    int added = 0;

    int removed = 0;

    int changed = 0;



    int totalChanges() const

    {

        return added + removed + changed;

    }

};



static pid_t findRobloxPlayer()

{

    int bufferSize =

        proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);



    if (bufferSize <= 0)

        return -1;



    std::vector<pid_t> pids(

        bufferSize / sizeof(pid_t)

    );



    int bytesUsed =

        proc_listpids(

            PROC_ALL_PIDS,

            0,

            pids.data(),

            static_cast<int>(

                pids.size() * sizeof(pid_t)

            )

        );



    int count =

        bytesUsed / sizeof(pid_t);



    for (int i = 0; i < count; ++i)

    {

        pid_t pid = pids[i];



        if (pid <= 0)

            continue;



        char name[PROC_PIDPATHINFO_MAXSIZE] = {};



        if (proc_name(

                pid,

                name,

                sizeof(name)) <= 0)

        {

            continue;

        }



        if (std::string(name) == "RobloxPlayer")

            return pid;

    }



    return -1;

}



static std::string protectionString(

    int protection)

{

    std::string result = "---";



    if (protection & 0x01)

        result[0] = 'r';



    if (protection & 0x02)

        result[1] = 'w';



    if (protection & 0x04)

        result[2] = 'x';



    return result;

}



static std::string sanitizeLabel(

    std::string label)

{

    for (char& c : label)

    {

        if (!std::isalnum(

                static_cast<unsigned char>(c)) &&

            c != '_' &&

            c != '-')

        {

            c = '_';

        }

    }



    return label;

}



static std::string makeSnapshotName(

    const std::string& label,

    int index)

{

    std::ostringstream name;



    name

        << "snapshots/"

        << label

        << "_"

        << std::setw(4)

        << std::setfill('0')

        << index

        << ".txt";



    return name.str();

}



static bool captureSnapshot(

    pid_t robloxPid,

    const std::string& label,

    int index)

{

    char executablePath[

        PROC_PIDPATHINFO_MAXSIZE

    ] = {};



    if (proc_pidpath(

            robloxPid,

            executablePath,

            sizeof(executablePath)) <= 0)

    {

        return false;

    }



    std::filesystem::create_directories(

        "snapshots"

    );



    std::string fileName =

        makeSnapshotName(label, index);



    std::ofstream output(fileName);



    if (!output)

        return false;



    output

        << "AEGISLAB_MEMORY_SNAPSHOT\n";



    output

        << "STATE\t"

        << label

        << "\n";



    output

        << "INDEX\t"

        << index

        << "\n";



    output

        << "PID\t"

        << robloxPid

        << "\n";



    output

        << "EXECUTABLE\t"

        << executablePath

        << "\n\n";



    output

        << "START\t"

        << "END\t"

        << "SIZE\t"

        << "PROTECTION\t"

        << "PATH\n";



    uint64_t address = 0;

    int regionCount = 0;



    while (true)

    {

        proc_regionwithpathinfo regionInfo{};



        int result =

            proc_pidinfo(

                robloxPid,

                PROC_PIDREGIONPATHINFO,

                address,

                &regionInfo,

                sizeof(regionInfo)

            );



        if (result <= 0)

            break;



        const auto& region =

            regionInfo.prp_prinfo;



        uint64_t start =

            region.pri_address;



        uint64_t size =

            region.pri_size;



        uint64_t end =

            start + size;



        std::string protection =

            protectionString(

                region.pri_protection

            );



        std::string path;



        if (regionInfo.prp_vip.vip_path[0] != '\0')

        {

            path =

                regionInfo.prp_vip.vip_path;

        }



        output

            << "0x"

            << std::hex

            << std::setw(16)

            << std::setfill('0')

            << start



            << "\t0x"

            << std::setw(16)

            << end



            << "\t0x"

            << std::setw(16)

            << size



            << std::dec

            << "\t"

            << protection

            << "\t"

            << path

            << "\n";



        ++regionCount;



        if (end <= address)

            break;



        address = end;

    }



    output

        << "\nREGION_COUNT\t"

        << regionCount

        << "\n";



    output.close();



    std::cout

        << "Captured "

        << fileName

        << " | Regions: "

        << regionCount

        << "\n";



    return true;

}



static bool parseHex(

    const std::string& text,

    uint64_t& value)

{

    try

    {

        value =

            std::stoull(

                text,

                nullptr,

                16

            );



        return true;

    }

    catch (...)

    {

        return false;

    }

}



static bool loadSnapshot(

    const std::string& fileName,

    std::vector<Region>& regions)

{

    std::ifstream input(fileName);



    if (!input)

        return false;



    std::string line;

    bool insideRegions = false;



    while (std::getline(input, line))

    {

        if (line.rfind("START\tEND\tSIZE", 0) == 0)

        {

            insideRegions = true;

            continue;

        }



        if (!insideRegions)

            continue;



        if (line.empty())

            continue;



        if (line.rfind("REGION_COUNT", 0) == 0)

            break;



        std::stringstream ss(line);



        std::string startText;

        std::string endText;

        std::string sizeText;

        std::string protection;

        std::string path;



        if (!std::getline(

                ss,

                startText,

                '\t'))

        {

            continue;

        }



        if (!std::getline(

                ss,

                endText,

                '\t'))

        {

            continue;

        }



        if (!std::getline(

                ss,

                sizeText,

                '\t'))

        {

            continue;

        }



        if (!std::getline(

                ss,

                protection,

                '\t'))

        {

            continue;

        }



        std::getline(ss, path);



        Region region;



        if (!parseHex(

                startText,

                region.start))

        {

            continue;

        }



        if (!parseHex(

                endText,

                region.end))

        {

            continue;

        }



        if (!parseHex(

                sizeText,

                region.size))

        {

            continue;

        }



        region.protection = protection;

        region.path = path;



        regions.push_back(region);

    }



    return true;

}



static ComparisonStats compareSnapshots(

    const std::vector<Region>& first,

    const std::vector<Region>& second,

    std::ostream& output)

{

    std::map<uint64_t, Region> firstMap;

    std::map<uint64_t, Region> secondMap;



    for (const Region& region : first)

        firstMap[region.start] = region;



    for (const Region& region : second)

        secondMap[region.start] = region;



    ComparisonStats stats;



    output << "\nADDED REGIONS\n";

    output << "-------------\n";



    for (const auto& [start, region] : secondMap)

    {

        if (!firstMap.contains(start))

        {

            ++stats.added;



            output

                << "+ 0x"

                << std::hex

                << region.start

                << " - 0x"

                << region.end

                << std::dec

                << "  "

                << region.protection;



            if (!region.path.empty())

            {

                output

                    << "  "

                    << region.path;

            }



            output << "\n";

        }

    }



    output << "\nREMOVED REGIONS\n";

    output << "---------------\n";



    for (const auto& [start, region] : firstMap)

    {

        if (!secondMap.contains(start))

        {

            ++stats.removed;



            output

                << "- 0x"

                << std::hex

                << region.start

                << " - 0x"

                << region.end

                << std::dec

                << "  "

                << region.protection;



            if (!region.path.empty())

            {

                output

                    << "  "

                    << region.path;

            }



            output << "\n";

        }

    }



    output << "\nCHANGED REGIONS\n";

    output << "---------------\n";



    for (const auto& [start, oldRegion] : firstMap)

    {

        auto it =

            secondMap.find(start);



        if (it == secondMap.end())

            continue;



        const Region& newRegion =

            it->second;



        bool changed =

            oldRegion.end != newRegion.end ||

            oldRegion.size != newRegion.size ||

            oldRegion.protection !=

                newRegion.protection ||

            oldRegion.path !=

                newRegion.path;



        if (!changed)

            continue;



        ++stats.changed;



        output

            << "* 0x"

            << std::hex

            << start

            << std::dec

            << "\n";



        if (oldRegion.size != newRegion.size)

        {

            output

                << "    size: 0x"

                << std::hex

                << oldRegion.size

                << " -> 0x"

                << newRegion.size

                << std::dec

                << "\n";

        }



        if (oldRegion.end != newRegion.end)

        {

            output

                << "    end: 0x"

                << std::hex

                << oldRegion.end

                << " -> 0x"

                << newRegion.end

                << std::dec

                << "\n";

        }



        if (oldRegion.protection !=

            newRegion.protection)

        {

            output

                << "    protection: "

                << oldRegion.protection

                << " -> "

                << newRegion.protection

                << "\n";

        }



        if (oldRegion.path != newRegion.path)

        {

            output

                << "    path: "

                << oldRegion.path

                << " -> "

                << newRegion.path

                << "\n";

        }

    }



    return stats;

}



static int compareSeries(

    const std::string& label)

{

    std::filesystem::create_directories(

        "reports"

    );



    std::string reportPath =

        "reports/" +

        label +

        "_compare.txt";



    std::ofstream report(reportPath);



    if (!report)

    {

        std::cerr

            << "Could not create report.\n";



        return 1;

    }



    struct SummaryEntry

    {

        int from = 0;

        int to = 0;



        ComparisonStats stats;

    };



    std::vector<SummaryEntry> summaries;



    for (int i = 1; i < 15; ++i)

    {

        std::string firstName =

            makeSnapshotName(

                label,

                i

            );



        std::string secondName =

            makeSnapshotName(

                label,

                i + 1

            );



        std::vector<Region> first;

        std::vector<Region> second;



        if (!loadSnapshot(

                firstName,

                first))

        {

            std::cerr

                << "Could not load "

                << firstName

                << "\n";



            return 1;

        }



        if (!loadSnapshot(

                secondName,

                second))

        {

            std::cerr

                << "Could not load "

                << secondName

                << "\n";



            return 1;

        }



        report

            << "\n========================================\n";



        report

            << "CAPTURE "

            << std::setw(4)

            << std::setfill('0')

            << i

            << " -> "

            << std::setw(4)

            << i + 1

            << "\n";



        report

            << "========================================\n";



        ComparisonStats stats =

            compareSnapshots(

                first,

                second,

                report

            );



        summaries.push_back(

            {

                i,

                i + 1,

                stats

            }

        );



        report

            << "\nSUMMARY\n"

            << "Added:   "

            << stats.added

            << "\nRemoved: "

            << stats.removed

            << "\nChanged: "

            << stats.changed

            << "\nTotal:   "

            << stats.totalChanges()

            << "\n";

    }



    std::sort(

        summaries.begin(),

        summaries.end(),

        [](

            const SummaryEntry& a,

            const SummaryEntry& b)

        {

            return

                a.stats.totalChanges() >

                b.stats.totalChanges();

        }

    );



    report

        << "\n\n"

        << "========================================\n"

        << "LARGEST TRANSITIONS\n"

        << "========================================\n";



    for (const auto& entry : summaries)

    {

        report

            << std::setw(4)

            << std::setfill('0')

            << entry.from

            << " -> "

            << std::setw(4)

            << entry.to

            << " | Total: "

            << entry.stats.totalChanges()

            << " | Added: "

            << entry.stats.added

            << " | Removed: "

            << entry.stats.removed

            << " | Changed: "

            << entry.stats.changed

            << "\n";

    }



    report.close();



    std::cout

        << "\nSeries comparison complete.\n"

        << "Compared 15 snapshots.\n"

        << "Transitions analyzed: 14\n\n"

        << "Largest transitions:\n";



    int shown = 0;



    for (const auto& entry : summaries)

    {

        std::cout

            << "  "

            << std::setw(4)

            << std::setfill('0')

            << entry.from

            << " -> "

            << std::setw(4)

            << entry.to

            << " | "

            << entry.stats.totalChanges()

            << " changes\n";



        ++shown;



        if (shown == 5)

            break;

    }



    std::cout

        << "\nFull report saved to:\n"

        << reportPath

        << "\n";



    return 0;

}



static std::string trimCopy(const std::string& text)

{

    std::size_t first = text.find_first_not_of(" \t\r\n");



    if (first == std::string::npos)

        return "";



    std::size_t last = text.find_last_not_of(" \t\r\n");



    return text.substr(

        first,

        last - first + 1

    );

}



static std::string lowerCopy(std::string text)

{

    std::transform(

        text.begin(),

        text.end(),

        text.begin(),

        [](unsigned char c)

        {

            return static_cast<char>(

                std::tolower(c)

            );

        }

    );



    return text;

}



static int runSdkCommand(

    const std::string& requestedClass)

{

    std::ifstream input(

        "generated/RobloxClasses.hpp"

    );



    if (!input)

    {

        std::cerr

            << "Could not open:\n"

            << "  generated/RobloxClasses.hpp\n\n"

            << "Run this first:\n"

            << "  python3 tools/generate_sdk.py\n";



        return 1;

    }



    std::string requestedLower =

        lowerCopy(requestedClass);



    std::string line;



    bool found = false;

    bool insideClass = false;



    std::string actualClassName;

    std::string superclass;



    std::vector<std::string> members;



    while (std::getline(input, line))

    {

        std::string trimmed =

            trimCopy(line);



        if (!insideClass)

        {

            if (trimmed.rfind("struct ", 0) != 0)

                continue;



            // Ignore forward declarations:

            //

            // struct Humanoid;

            //

            if (!trimmed.empty() &&

                trimmed.back() == ';')

            {

                continue;

            }



            std::string declaration =

                trimmed.substr(7);



            std::size_t nameEnd =

                declaration.find_first_of(

                    " :{"

                );



            if (nameEnd == std::string::npos)

                continue;



            std::string candidateName =

                declaration.substr(

                    0,

                    nameEnd

                );



            if (lowerCopy(candidateName) !=

                requestedLower)

            {

                continue;

            }



            found = true;

            insideClass = true;



            actualClassName =

                candidateName;



            std::size_t inheritance =

                declaration.find(

                    ": public "

                );



            if (inheritance !=

                std::string::npos)

            {

                superclass =

                    declaration.substr(

                        inheritance + 9

                    );



                superclass =

                    trimCopy(superclass);



                std::size_t extra =

                    superclass.find_first_of(

                        " {"

                    );



                if (extra !=

                    std::string::npos)

                {

                    superclass =

                        superclass.substr(

                            0,

                            extra

                        );

                }

            }



            continue;

        }



        if (trimmed == "};")

            break;



        if (trimmed.rfind("// ", 0) == 0)

        {

            std::string member =

                trimmed.substr(3);



            // Don't show this generated marker as

            // though it were a Roblox member.

            if (member ==

                "Generated API model.")

            {

                continue;

            }



            members.push_back(member);

        }

    }



    if (!found)

    {

        std::cerr

            << "SDK class not found: "

            << requestedClass

            << "\n";



        return 1;

    }



    std::cout

        << "\nAegisLab Roblox SDK\n"

        << "===================\n\n"

        << "Class: "

        << actualClassName

        << "\n";



    if (!superclass.empty())

    {

        std::cout

            << "Superclass: "

            << superclass

            << "\n";

    }

    else

    {

        std::cout

            << "Superclass: <none>\n";

    }



    std::cout << "\n";



    if (members.empty())

    {

        std::cout

            << "No API members were listed "

            << "directly on this class.\n";



        return 0;

    }



    for (const std::string& member : members)

    {

        if (member == "Properties" ||

            member == "Functions" ||

            member == "Events")

        {

            std::cout

                << "\n"

                << member

                << "\n"

                << std::string(

                    member.size(),

                    '-'

                )

                << "\n";



            continue;

        }



        std::cout

            << "  "

            << member

            << "\n";

    }



    std::cout << "\n";



    return 0;

}




static bool getSdkSuperclass(
    const std::string& requestedClass,
    std::string& actualClassName,
    std::string& superclass)
{
    std::ifstream input(
        "generated/RobloxClasses.hpp"
    );

    if (!input)
        return false;

    std::string requestedLower =
        lowerCopy(requestedClass);

    std::string line;

    while (std::getline(input, line))
    {
        std::string trimmed = trimCopy(line);

        if (trimmed.rfind("struct ", 0) != 0)
            continue;

        if (!trimmed.empty() &&
            trimmed.back() == ';')
        {
            continue;
        }

        std::string declaration =
            trimmed.substr(7);

        std::size_t nameEnd =
            declaration.find_first_of(" :{");

        if (nameEnd == std::string::npos)
            continue;

        std::string candidateName =
            declaration.substr(0, nameEnd);

        if (lowerCopy(candidateName) != requestedLower)
            continue;

        actualClassName = candidateName;
        superclass.clear();

        std::size_t inheritance =
            declaration.find(": public ");

        if (inheritance != std::string::npos)
        {
            superclass = trimCopy(
                declaration.substr(inheritance + 9)
            );

            std::size_t extra =
                superclass.find_first_of(" {");

            if (extra != std::string::npos)
            {
                superclass =
                    superclass.substr(0, extra);
            }
        }

        return true;
    }

    return false;
}

static int runSdkTreeCommand(
    const std::string& requestedClass)
{
    std::vector<std::string> chain;
    std::string current = requestedClass;

    for (int depth = 0; depth < 128; ++depth)
    {
        std::string actual;
        std::string superclass;

        if (!getSdkSuperclass(
                current,
                actual,
                superclass))
        {
            if (chain.empty())
            {
                std::cerr
                    << "SDK class not found: "
                    << requestedClass
                    << "\n";

                return 1;
            }

            break;
        }

        chain.push_back(actual);

        if (superclass.empty())
            break;

        current = superclass;
    }

    std::reverse(
        chain.begin(),
        chain.end()
    );

    std::cout
        << "\nAegisLab Roblox SDK Tree\n"
        << "========================\n\n";

    for (std::size_t i = 0; i < chain.size(); ++i)
    {
        for (std::size_t indent = 0;
             indent < i;
             ++indent)
        {
            std::cout << "    ";
        }

        if (i > 0)
            std::cout << "└── ";

        std::cout
            << chain[i]
            << "\n";
    }

    std::cout << "\n";

    return 0;
}

static int runSdkFindCommand(
    const std::string& searchText)
{
    std::ifstream input(
        "generated/RobloxClasses.hpp"
    );

    if (!input)
    {
        std::cerr
            << "Could not open:\n"
            << "  generated/RobloxClasses.hpp\n\n"
            << "Run this first:\n"
            << "  python3 tools/generate_sdk.py\n";

        return 1;
    }

    const std::string needle =
        lowerCopy(searchText);

    std::string line;
    std::string currentClass;
    bool insideClass = false;
    int matchCount = 0;

    std::cout
        << "\nAegisLab Roblox SDK Search\n"
        << "==========================\n"
        << "Search: "
        << searchText
        << "\n\n";

    while (std::getline(input, line))
    {
        std::string trimmed = trimCopy(line);

        if (trimmed.rfind("struct ", 0) == 0 &&
            !trimmed.empty() &&
            trimmed.back() != ';')
        {
            std::string declaration =
                trimmed.substr(7);

            std::size_t nameEnd =
                declaration.find_first_of(" :{");

            if (nameEnd != std::string::npos)
            {
                currentClass =
                    declaration.substr(0, nameEnd);

                insideClass = true;

                if (lowerCopy(currentClass).find(needle) !=
                    std::string::npos)
                {
                    std::cout
                        << currentClass
                        << "  [class name]\n";

                    ++matchCount;
                }
            }

            continue;
        }

        if (insideClass && trimmed == "};")
        {
            insideClass = false;
            currentClass.clear();
            continue;
        }

        if (!insideClass ||
            trimmed.rfind("// ", 0) != 0)
        {
            continue;
        }

        std::string member =
            trimmed.substr(3);

        if (member == "Properties" ||
            member == "Functions" ||
            member == "Events" ||
            member == "Generated API model.")
        {
            continue;
        }

        if (lowerCopy(member).find(needle) ==
            std::string::npos)
        {
            continue;
        }

        std::cout
            << currentClass
            << " :: "
            << member
            << "\n";

        ++matchCount;
    }

    std::cout
        << "\nMatches: "
        << matchCount
        << "\n\n";

    return matchCount > 0 ? 0 : 1;
}



static std::string machProtection(vm_prot_t protection)
{
    std::string result;

    result +=
        (protection & VM_PROT_READ)
            ? 'r'
            : '-';

    result +=
        (protection & VM_PROT_WRITE)
            ? 'w'
            : '-';

    result +=
        (protection & VM_PROT_EXECUTE)
            ? 'x'
            : '-';

    return result;
}

static std::string machCpuName(cpu_type_t cpuType)
{
    switch (cpuType)
    {
        case CPU_TYPE_ARM64:
            return "arm64";

        case CPU_TYPE_X86_64:
            return "x86_64";

        default:
            return "unknown";
    }
}

static int runMachOInfo(
    const std::string& executablePath)
{
    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
    {
        std::cerr
            << "Could not open Mach-O file:\n"
            << "  "
            << executablePath
            << "\n";

        return 1;
    }

    uint32_t magic = 0;

    input.read(
        reinterpret_cast<char*>(&magic),
        sizeof(magic)
    );

    if (!input)
    {
        std::cerr
            << "Could not read Mach-O magic.\n";

        return 1;
    }

    if (magic == FAT_MAGIC ||
        magic == FAT_CIGAM ||
        magic == FAT_MAGIC_64 ||
        magic == FAT_CIGAM_64)
    {
        std::cerr
            << "This RobloxPlayer is a universal/fat Mach-O.\n"
            << "This AegisLab command currently expects the "
            << "thin arm64 executable used by the installed "
            << "Roblox client.\n";

        return 1;
    }

    if (magic != MH_MAGIC_64)
    {
        std::cerr
            << "Unsupported Mach-O format or byte order.\n"
            << "Magic: 0x"
            << std::hex
            << magic
            << std::dec
            << "\n";

        return 1;
    }

    input.seekg(0);

    mach_header_64 header{};

    input.read(
        reinterpret_cast<char*>(&header),
        sizeof(header)
    );

    if (!input)
    {
        std::cerr
            << "Could not read Mach-O header.\n";

        return 1;
    }

    struct SegmentSummary
    {
        std::string name;
        uint64_t vmaddr = 0;
        uint64_t vmsize = 0;
        uint64_t fileoff = 0;
        uint64_t filesize = 0;
        vm_prot_t maxprot = 0;
        vm_prot_t initprot = 0;
        uint32_t nsects = 0;
    };

    std::vector<SegmentSummary> segments;

    uint64_t preferredTextBase = 0;
    bool foundText = false;

    std::cout
        << "\nAegisLab Mach-O Inspector\n"
        << "=========================\n\n"
        << "File: "
        << executablePath
        << "\n"
        << "Architecture: "
        << machCpuName(header.cputype)
        << "\n"
        << "CPU type: "
        << header.cputype
        << "\n"
        << "CPU subtype: "
        << header.cpusubtype
        << "\n"
        << "File type: "
        << header.filetype
        << "\n"
        << "Load commands: "
        << header.ncmds
        << "\n"
        << "Load-command bytes: "
        << header.sizeofcmds
        << "\n\n";

    std::streamoff commandOffset =
        static_cast<std::streamoff>(
            sizeof(mach_header_64)
        );

    for (uint32_t i = 0;
         i < header.ncmds;
         ++i)
    {
        input.seekg(commandOffset);

        load_command command{};

        input.read(
            reinterpret_cast<char*>(&command),
            sizeof(command)
        );

        if (!input ||
            command.cmdsize < sizeof(load_command))
        {
            std::cerr
                << "Invalid Mach-O load command at index "
                << i
                << ".\n";

            return 1;
        }

        if (command.cmd == LC_SEGMENT_64)
        {
            input.seekg(commandOffset);

            segment_command_64 segment{};

            input.read(
                reinterpret_cast<char*>(&segment),
                sizeof(segment)
            );

            if (!input)
            {
                std::cerr
                    << "Could not read LC_SEGMENT_64.\n";

                return 1;
            }

            std::string segmentName(
                segment.segname,
                strnlen(
                    segment.segname,
                    sizeof(segment.segname)
                )
            );

            SegmentSummary summary;

            summary.name = segmentName;
            summary.vmaddr = segment.vmaddr;
            summary.vmsize = segment.vmsize;
            summary.fileoff = segment.fileoff;
            summary.filesize = segment.filesize;
            summary.maxprot = segment.maxprot;
            summary.initprot = segment.initprot;
            summary.nsects = segment.nsects;

            segments.push_back(summary);

            if (segmentName == "__TEXT")
            {
                preferredTextBase = segment.vmaddr;
                foundText = true;
            }

            std::cout
                << "Segment "
                << segmentName
                << "\n"
                << "  VM address:  0x"
                << std::hex
                << segment.vmaddr
                << "\n"
                << "  VM size:     0x"
                << segment.vmsize
                << "\n"
                << "  File offset: 0x"
                << segment.fileoff
                << "\n"
                << "  File size:   0x"
                << segment.filesize
                << std::dec
                << "\n"
                << "  Protection:  "
                << machProtection(
                    segment.initprot
                )
                << "  (max "
                << machProtection(
                    segment.maxprot
                )
                << ")\n"
                << "  Sections:    "
                << segment.nsects
                << "\n";

            std::streamoff sectionOffset =
                commandOffset +
                static_cast<std::streamoff>(
                    sizeof(segment_command_64)
                );

            for (uint32_t sectionIndex = 0;
                 sectionIndex < segment.nsects;
                 ++sectionIndex)
            {
                input.seekg(sectionOffset);

                section_64 section{};

                input.read(
                    reinterpret_cast<char*>(&section),
                    sizeof(section)
                );

                if (!input)
                {
                    std::cerr
                        << "Could not read Mach-O section.\n";

                    return 1;
                }

                std::string sectionName(
                    section.sectname,
                    strnlen(
                        section.sectname,
                        sizeof(section.sectname)
                    )
                );

                std::cout
                    << "    "
                    << sectionName
                    << "\n"
                    << "      VM address:  0x"
                    << std::hex
                    << section.addr
                    << "\n"
                    << "      Size:        0x"
                    << section.size
                    << "\n"
                    << "      File offset: 0x"
                    << section.offset
                    << std::dec
                    << "\n";

                sectionOffset +=
                    static_cast<std::streamoff>(
                        sizeof(section_64)
                    );
            }

            std::cout << "\n";
        }

        commandOffset +=
            static_cast<std::streamoff>(
                command.cmdsize
            );
    }

    if (!foundText)
    {
        std::cerr
            << "__TEXT segment was not found.\n";

        return 1;
    }

    std::cout
        << "Module-relative segment map\n"
        << "---------------------------\n"
        << "Preferred __TEXT base: 0x"
        << std::hex
        << preferredTextBase
        << std::dec
        << "\n\n";

    for (const SegmentSummary& segment : segments)
    {
        if (segment.vmaddr < preferredTextBase)
            continue;

        std::cout
            << std::left
            << std::setw(16)
            << segment.name
            << " +0x"
            << std::hex
            << (segment.vmaddr -
                preferredTextBase)
            << std::dec
            << "\n";
    }

    std::cout
        << "\nUse live-info to get the current ASLR-loaded "
        << "Roblox image base.\n"
        << "For the same build, a module-relative address "
        << "can then be translated to a live address as:\n\n"
        << "  live_address = live_image_base + relative_offset\n\n";

    return 0;
}


struct MachSegmentInfo
{
    std::string name;
    uint64_t vmaddr = 0;
    uint64_t vmsize = 0;
};

static bool loadMachAddressLayout(
    const std::string& executablePath,
    uint64_t& preferredTextBase,
    std::vector<MachSegmentInfo>& segments)
{
    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
        return false;

    mach_header_64 header{};

    input.read(
        reinterpret_cast<char*>(&header),
        sizeof(header)
    );

    if (!input ||
        header.magic != MH_MAGIC_64)
    {
        return false;
    }

    preferredTextBase = 0;
    bool foundText = false;

    std::streamoff commandOffset =
        static_cast<std::streamoff>(
            sizeof(mach_header_64)
        );

    for (uint32_t i = 0;
         i < header.ncmds;
         ++i)
    {
        input.seekg(commandOffset);

        load_command command{};

        input.read(
            reinterpret_cast<char*>(&command),
            sizeof(command)
        );

        if (!input ||
            command.cmdsize < sizeof(load_command))
        {
            return false;
        }

        if (command.cmd == LC_SEGMENT_64)
        {
            input.seekg(commandOffset);

            segment_command_64 segment{};

            input.read(
                reinterpret_cast<char*>(&segment),
                sizeof(segment)
            );

            if (!input)
                return false;

            std::string segmentName(
                segment.segname,
                strnlen(
                    segment.segname,
                    sizeof(segment.segname)
                )
            );

            MachSegmentInfo info;

            info.name = segmentName;
            info.vmaddr = segment.vmaddr;
            info.vmsize = segment.vmsize;

            segments.push_back(info);

            if (segmentName == "__TEXT")
            {
                preferredTextBase =
                    segment.vmaddr;

                foundText = true;
            }
        }

        commandOffset +=
            static_cast<std::streamoff>(
                command.cmdsize
            );
    }

    return foundText;
}

static bool findLiveImageBase(
    pid_t robloxPid,
    const std::string& executablePath,
    uint64_t& liveImageBase)
{
    uint64_t address = 0;
    bool found = false;
    uint64_t lowest = 0;

    while (true)
    {
        proc_regionwithpathinfo regionInfo{};

        int result =
            proc_pidinfo(
                robloxPid,
                PROC_PIDREGIONPATHINFO,
                address,
                &regionInfo,
                sizeof(regionInfo)
            );

        if (result <= 0)
            break;

        const auto& region =
            regionInfo.prp_prinfo;

        uint64_t start =
            region.pri_address;

        uint64_t end =
            start + region.pri_size;

        std::string path;

        if (regionInfo.prp_vip.vip_path[0] != '\0')
            path = regionInfo.prp_vip.vip_path;

        if (path == executablePath)
        {
            if (!found ||
                start < lowest)
            {
                lowest = start;
                found = true;
            }
        }

        if (end <= address)
            break;

        address = end;
    }

    if (!found)
        return false;

    liveImageBase = lowest;

    return true;
}

static bool parseRelativeOffset(
    std::string text,
    uint64_t& value)
{
    text = trimCopy(text);

    if (!text.empty() &&
        text.front() == '+')
    {
        text.erase(
            text.begin()
        );
    }

    if (text.size() >= 2 &&
        text[0] == '0' &&
        (text[1] == 'x' ||
         text[1] == 'X'))
    {
        text.erase(0, 2);
    }

    if (text.empty())
        return false;

    try
    {
        size_t consumed = 0;

        value =
            std::stoull(
                text,
                &consumed,
                16
            );

        return consumed ==
            text.size();
    }
    catch (...)
    {
        return false;
    }
}

struct MachSectionInfo
{
    std::string segmentName;
    std::string sectionName;
    uint64_t vmaddr = 0;
    uint64_t size = 0;
    uint32_t fileOffset = 0;
    uint32_t flags = 0;
    uint32_t reserved1 = 0;
    uint32_t reserved2 = 0;
};

static bool loadMachSectionLayout(
    const std::string& executablePath,
    uint64_t& preferredTextBase,
    std::vector<MachSegmentInfo>& segments,
    std::vector<MachSectionInfo>& sections)
{
    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
        return false;

    mach_header_64 header{};

    input.read(
        reinterpret_cast<char*>(&header),
        sizeof(header)
    );

    if (!input ||
        header.magic != MH_MAGIC_64)
    {
        return false;
    }

    preferredTextBase = 0;
    bool foundText = false;

    std::streamoff commandOffset =
        static_cast<std::streamoff>(
            sizeof(mach_header_64)
        );

    for (uint32_t i = 0;
         i < header.ncmds;
         ++i)
    {
        input.seekg(commandOffset);

        load_command command{};

        input.read(
            reinterpret_cast<char*>(&command),
            sizeof(command)
        );

        if (!input ||
            command.cmdsize < sizeof(load_command))
        {
            return false;
        }

        if (command.cmd == LC_SEGMENT_64)
        {
            input.seekg(commandOffset);

            segment_command_64 segment{};

            input.read(
                reinterpret_cast<char*>(&segment),
                sizeof(segment)
            );

            if (!input)
                return false;

            std::string segmentName(
                segment.segname,
                strnlen(
                    segment.segname,
                    sizeof(segment.segname)
                )
            );

            MachSegmentInfo segmentInfo;

            segmentInfo.name = segmentName;
            segmentInfo.vmaddr = segment.vmaddr;
            segmentInfo.vmsize = segment.vmsize;

            segments.push_back(segmentInfo);

            if (segmentName == "__TEXT")
            {
                preferredTextBase =
                    segment.vmaddr;

                foundText = true;
            }

            std::streamoff sectionOffset =
                commandOffset +
                static_cast<std::streamoff>(
                    sizeof(segment_command_64)
                );

            for (uint32_t sectionIndex = 0;
                 sectionIndex < segment.nsects;
                 ++sectionIndex)
            {
                input.seekg(sectionOffset);

                section_64 section{};

                input.read(
                    reinterpret_cast<char*>(&section),
                    sizeof(section)
                );

                if (!input)
                    return false;

                MachSectionInfo sectionInfo;

                sectionInfo.segmentName =
                    std::string(
                        section.segname,
                        strnlen(
                            section.segname,
                            sizeof(section.segname)
                        )
                    );

                sectionInfo.sectionName =
                    std::string(
                        section.sectname,
                        strnlen(
                            section.sectname,
                            sizeof(section.sectname)
                        )
                    );

                sectionInfo.vmaddr =
                    section.addr;

                sectionInfo.size =
                    section.size;

                sectionInfo.fileOffset =
                    section.offset;

                sectionInfo.flags =
                    section.flags;

                sectionInfo.reserved1 =
                    section.reserved1;

                sectionInfo.reserved2 =
                    section.reserved2;

                sections.push_back(sectionInfo);

                sectionOffset +=
                    static_cast<std::streamoff>(
                        sizeof(section_64)
                    );
            }
        }

        commandOffset +=
            static_cast<std::streamoff>(
                command.cmdsize
            );
    }

    return foundText;
}

static std::string describeMachLocation(
    uint64_t preferredAddress,
    const std::vector<MachSegmentInfo>& segments,
    const std::vector<MachSectionInfo>& sections)
{
    for (const MachSectionInfo& section :
         sections)
    {
        if (preferredAddress >=
                section.vmaddr &&
            preferredAddress -
                section.vmaddr <
                section.size)
        {
            std::ostringstream out;

            out
                << section.segmentName
                << "::"
                << section.sectionName
                << " + 0x"
                << std::hex
                << (preferredAddress -
                    section.vmaddr);

            return out.str();
        }
    }

    for (const MachSegmentInfo& segment :
         segments)
    {
        if (preferredAddress >=
                segment.vmaddr &&
            preferredAddress -
                segment.vmaddr <
                segment.vmsize)
        {
            std::ostringstream out;

            out
                << segment.name
                << " + 0x"
                << std::hex
                << (preferredAddress -
                    segment.vmaddr);

            return out.str();
        }
    }

    return "(outside known Mach-O segments)";
}

static int runMachOFind(
    const std::string& executablePath,
    const std::string& searchText)
{
    if (searchText.empty())
    {
        std::cerr
            << "Search text cannot be empty.\n";

        return 1;
    }

    uint64_t preferredTextBase = 0;
    std::vector<MachSegmentInfo> segments;
    std::vector<MachSectionInfo> sections;

    if (!loadMachSectionLayout(
            executablePath,
            preferredTextBase,
            segments,
            sections))
    {
        std::cerr
            << "Could not read the Roblox Mach-O layout.\n";

        return 1;
    }

    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
    {
        std::cerr
            << "Could not open Mach-O file:\n"
            << "  "
            << executablePath
            << "\n";

        return 1;
    }

    input.seekg(
        0,
        std::ios::end
    );

    std::streamoff fileSize =
        input.tellg();

    if (fileSize <= 0)
    {
        std::cerr
            << "Mach-O file is empty or unreadable.\n";

        return 1;
    }

    input.seekg(0);

    std::vector<char> bytes(
        static_cast<size_t>(
            fileSize
        )
    );

    input.read(
        bytes.data(),
        fileSize
    );

    if (!input)
    {
        std::cerr
            << "Could not read the Mach-O file.\n";

        return 1;
    }

    std::cout
        << "\nAegisLab Mach-O String Search\n"
        << "=============================\n\n"
        << "File: "
        << executablePath
        << "\n"
        << "Search: "
        << searchText
        << "\n\n";

    size_t matches = 0;

    for (size_t i = 0;
         i + searchText.size() <= bytes.size();
         ++i)
    {
        bool same = true;

        for (size_t j = 0;
             j < searchText.size();
             ++j)
        {
            if (bytes[i + j] != searchText[j])
            {
                same = false;
                break;
            }
        }

        if (!same)
            continue;

        uint64_t fileOffset =
            static_cast<uint64_t>(i);

        bool mapped = false;
        uint64_t preferredAddress = 0;
        std::string location =
            "(file offset is not inside a mapped section)";

        for (const MachSectionInfo& section :
             sections)
        {
            if (section.size == 0)
                continue;

            uint64_t sectionFileStart =
                section.fileOffset;

            uint64_t sectionFileEnd =
                sectionFileStart +
                section.size;

            if (fileOffset >=
                    sectionFileStart &&
                fileOffset <
                    sectionFileEnd)
            {
                uint64_t within =
                    fileOffset -
                    sectionFileStart;

                preferredAddress =
                    section.vmaddr +
                    within;

                location =
                    describeMachLocation(
                        preferredAddress,
                        segments,
                        sections
                    );

                mapped = true;
                break;
            }
        }

        ++matches;

        std::cout
            << "Match "
            << matches
            << "\n"
            << "  File offset:    0x"
            << std::hex
            << fileOffset
            << std::dec
            << "\n";

        if (mapped)
        {
            uint64_t relativeOffset =
                preferredAddress -
                preferredTextBase;

            std::cout
                << "  Preferred addr: 0x"
                << std::hex
                << preferredAddress
                << "\n"
                << "  Relative:      +0x"
                << relativeOffset
                << std::dec
                << "\n"
                << "  Location:       "
                << location
                << "\n";
        }
        else
        {
            std::cout
                << "  Preferred addr: (not mapped)\n"
                << "  Relative:       (not mapped)\n"
                << "  Location:       "
                << location
                << "\n";
        }

        std::cout << "\n";

        // Skip ahead so overlapping matches of the same literal
        // are not reported repeatedly.
        if (!searchText.empty())
            i += searchText.size() - 1;
    }

    std::cout
        << "Matches: "
        << matches
        << "\n\n";

    return 0;
}


static int runAddressTranslator(
    pid_t robloxPid,
    const std::string& offsetText)
{
    char executablePathBuffer[
        PROC_PIDPATHINFO_MAXSIZE
    ] = {};

    if (proc_pidpath(
            robloxPid,
            executablePathBuffer,
            sizeof(executablePathBuffer)) <= 0)
    {
        std::cerr
            << "Could not read RobloxPlayer executable path.\n";

        return 1;
    }

    std::string executablePath =
        executablePathBuffer;

    uint64_t relativeOffset = 0;

    if (!parseRelativeOffset(
            offsetText,
            relativeOffset))
    {
        std::cerr
            << "Invalid relative offset: "
            << offsetText
            << "\n\n"
            << "Examples:\n"
            << "  ./build/AegisLab addr +0x65f0000\n"
            << "  ./build/AegisLab addr 0x65f0000\n";

        return 1;
    }

    uint64_t preferredTextBase = 0;
    std::vector<MachSegmentInfo> segments;
    std::vector<MachSectionInfo> sections;

    if (!loadMachSectionLayout(
            executablePath,
            preferredTextBase,
            segments,
            sections))
    {
        std::cerr
            << "Could not read the Roblox Mach-O layout.\n";

        return 1;
    }

    uint64_t liveImageBase = 0;

    if (!findLiveImageBase(
            robloxPid,
            executablePath,
            liveImageBase))
    {
        std::cerr
            << "Could not locate the live Roblox image base.\n";

        return 1;
    }

    if (relativeOffset >
        UINT64_MAX - preferredTextBase ||
        relativeOffset >
        UINT64_MAX - liveImageBase)
    {
        std::cerr
            << "Offset is too large.\n";

        return 1;
    }

    uint64_t preferredAddress =
        preferredTextBase +
        relativeOffset;

    uint64_t liveAddress =
        liveImageBase +
        relativeOffset;

    uint64_t aslrSlide = 0;

    if (liveImageBase >=
        preferredTextBase)
    {
        aslrSlide =
            liveImageBase -
            preferredTextBase;
    }

    std::string location =
        describeMachLocation(
            preferredAddress,
            segments,
            sections
        );

    std::cout
        << "\nAegisLab Address Translator\n"
        << "===========================\n\n"
        << "Target: RobloxPlayer\n"
        << "PID: "
        << robloxPid
        << "\n"
        << "Path: "
        << executablePath
        << "\n\n"
        << std::hex
        << "Relative:       +0x"
        << relativeOffset
        << "\n"
        << "Preferred base:  0x"
        << preferredTextBase
        << "\n"
        << "Preferred addr:  0x"
        << preferredAddress
        << "\n"
        << "Live base:       0x"
        << liveImageBase
        << "\n"
        << "ASLR slide:      0x"
        << aslrSlide
        << "\n"
        << "Live address:    0x"
        << liveAddress
        << "\n"
        << std::dec
        << "Location:        "
        << location
        << "\n\n";

    return 0;
}


static bool parseAbsoluteHexAddress(
    std::string text,
    uint64_t& value)
{
    text = trimCopy(text);

    if (text.size() >= 2 &&
        text[0] == '0' &&
        (text[1] == 'x' ||
         text[1] == 'X'))
    {
        text.erase(0, 2);
    }

    if (text.empty())
        return false;

    try
    {
        size_t consumed = 0;

        value =
            std::stoull(
                text,
                &consumed,
                16
            );

        return consumed ==
            text.size();
    }
    catch (...)
    {
        return false;
    }
}

static int runLiveAddressTranslator(
    pid_t robloxPid,
    const std::string& addressText)
{
    char executablePathBuffer[
        PROC_PIDPATHINFO_MAXSIZE
    ] = {};

    if (proc_pidpath(
            robloxPid,
            executablePathBuffer,
            sizeof(executablePathBuffer)) <= 0)
    {
        std::cerr
            << "Could not read RobloxPlayer executable path.\n";

        return 1;
    }

    std::string executablePath =
        executablePathBuffer;

    uint64_t liveAddress = 0;

    if (!parseAbsoluteHexAddress(
            addressText,
            liveAddress))
    {
        std::cerr
            << "Invalid live address: "
            << addressText
            << "\n\n"
            << "Example:\n"
            << "  ./build/AegisLab addr-live 0x10b07c000\n";

        return 1;
    }

    uint64_t preferredTextBase = 0;
    std::vector<MachSegmentInfo> segments;
    std::vector<MachSectionInfo> sections;

    if (!loadMachSectionLayout(
            executablePath,
            preferredTextBase,
            segments,
            sections))
    {
        std::cerr
            << "Could not read the Roblox Mach-O layout.\n";

        return 1;
    }

    uint64_t liveImageBase = 0;

    if (!findLiveImageBase(
            robloxPid,
            executablePath,
            liveImageBase))
    {
        std::cerr
            << "Could not locate the live Roblox image base.\n";

        return 1;
    }

    if (liveAddress < liveImageBase)
    {
        std::cerr
            << "Live address 0x"
            << std::hex
            << liveAddress
            << std::dec
            << " is below the current Roblox image base.\n";

        return 1;
    }

    uint64_t relativeOffset =
        liveAddress -
        liveImageBase;

    if (relativeOffset >
        UINT64_MAX - preferredTextBase)
    {
        std::cerr
            << "Address translation overflow.\n";

        return 1;
    }

    uint64_t preferredAddress =
        preferredTextBase +
        relativeOffset;

    uint64_t aslrSlide = 0;

    if (liveImageBase >= preferredTextBase)
    {
        aslrSlide =
            liveImageBase -
            preferredTextBase;
    }

    std::string location =
        describeMachLocation(
            preferredAddress,
            segments,
            sections
        );

    std::cout
        << "\nAegisLab Live Address Translator\n"
        << "================================\n\n"
        << "Target: RobloxPlayer\n"
        << "PID: "
        << robloxPid
        << "\n"
        << "Path: "
        << executablePath
        << "\n\n"
        << std::hex
        << "Live address:    0x"
        << liveAddress
        << "\n"
        << "Live base:       0x"
        << liveImageBase
        << "\n"
        << "ASLR slide:      0x"
        << aslrSlide
        << "\n"
        << "Relative:       +0x"
        << relativeOffset
        << "\n"
        << "Preferred base:  0x"
        << preferredTextBase
        << "\n"
        << "Preferred addr:  0x"
        << preferredAddress
        << "\n"
        << std::dec
        << "Location:        "
        << location
        << "\n\n";

    return 0;
}



static bool isPrintableCStringByte(unsigned char c)
{
    return c >= 0x20 && c <= 0x7e;
}

static bool mapFileOffsetToMachLocation(
    uint64_t fileOffset,
    uint64_t preferredTextBase,
    const std::vector<MachSegmentInfo>& segments,
    const std::vector<MachSectionInfo>& sections,
    uint64_t& preferredAddress,
    uint64_t& relativeOffset,
    std::string& location)
{
    for (const MachSectionInfo& section :
         sections)
    {
        if (section.size == 0)
            continue;

        uint64_t sectionFileStart =
            section.fileOffset;

        uint64_t sectionFileEnd =
            sectionFileStart +
            section.size;

        if (fileOffset >=
                sectionFileStart &&
            fileOffset <
                sectionFileEnd)
        {
            uint64_t within =
                fileOffset -
                sectionFileStart;

            preferredAddress =
                section.vmaddr +
                within;

            if (preferredAddress <
                preferredTextBase)
            {
                return false;
            }

            relativeOffset =
                preferredAddress -
                preferredTextBase;

            location =
                describeMachLocation(
                    preferredAddress,
                    segments,
                    sections
                );

            return true;
        }
    }

    return false;
}

static int runMachOFindExact(
    const std::string& executablePath,
    const std::string& searchText)
{
    if (searchText.empty())
    {
        std::cerr
            << "Search text cannot be empty.\n";

        return 1;
    }

    uint64_t preferredTextBase = 0;
    std::vector<MachSegmentInfo> segments;
    std::vector<MachSectionInfo> sections;

    if (!loadMachSectionLayout(
            executablePath,
            preferredTextBase,
            segments,
            sections))
    {
        std::cerr
            << "Could not read the Roblox Mach-O layout.\n";

        return 1;
    }

    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
    {
        std::cerr
            << "Could not open Mach-O file:\n"
            << "  "
            << executablePath
            << "\n";

        return 1;
    }

    input.seekg(0, std::ios::end);

    std::streamoff fileSize =
        input.tellg();

    if (fileSize <= 0)
    {
        std::cerr
            << "Mach-O file is empty or unreadable.\n";

        return 1;
    }

    input.seekg(0);

    std::vector<char> bytes(
        static_cast<size_t>(fileSize)
    );

    input.read(
        bytes.data(),
        fileSize
    );

    if (!input)
    {
        std::cerr
            << "Could not read the Mach-O file.\n";

        return 1;
    }

    std::cout
        << "\nAegisLab Exact C-String Search\n"
        << "==============================\n\n"
        << "File: "
        << executablePath
        << "\n"
        << "Search: "
        << searchText
        << "\n\n";

    size_t matches = 0;

    for (size_t i = 0;
         i + searchText.size() < bytes.size();
         ++i)
    {
        // Exact C-string means either file start or a null byte
        // immediately before the string, plus a null byte immediately after.
        if (i > 0 &&
            bytes[i - 1] != '\0')
        {
            continue;
        }

        bool same = true;

        for (size_t j = 0;
             j < searchText.size();
             ++j)
        {
            if (bytes[i + j] !=
                searchText[j])
            {
                same = false;
                break;
            }
        }

        if (!same)
            continue;

        if (bytes[i + searchText.size()] != '\0')
            continue;

        ++matches;

        uint64_t fileOffset =
            static_cast<uint64_t>(i);

        uint64_t preferredAddress = 0;
        uint64_t relativeOffset = 0;
        std::string location;

        bool mapped =
            mapFileOffsetToMachLocation(
                fileOffset,
                preferredTextBase,
                segments,
                sections,
                preferredAddress,
                relativeOffset,
                location
            );

        std::cout
            << "Match "
            << matches
            << "\n"
            << "  File offset:    0x"
            << std::hex
            << fileOffset
            << std::dec
            << "\n";

        if (mapped)
        {
            std::cout
                << "  Preferred addr: 0x"
                << std::hex
                << preferredAddress
                << "\n"
                << "  Relative:      +0x"
                << relativeOffset
                << std::dec
                << "\n"
                << "  Location:       "
                << location
                << "\n";
        }
        else
        {
            std::cout
                << "  Preferred addr: (not mapped)\n"
                << "  Relative:       (not mapped)\n"
                << "  Location:       (file offset is not inside a mapped section)\n";
        }

        std::cout << "\n";

        i += searchText.size();
    }

    std::cout
        << "Matches: "
        << matches
        << "\n\n";

    return 0;
}

static int runMachOFindContext(
    const std::string& executablePath,
    const std::string& searchText)
{
    if (searchText.empty())
    {
        std::cerr
            << "Search text cannot be empty.\n";

        return 1;
    }

    uint64_t preferredTextBase = 0;
    std::vector<MachSegmentInfo> segments;
    std::vector<MachSectionInfo> sections;

    if (!loadMachSectionLayout(
            executablePath,
            preferredTextBase,
            segments,
            sections))
    {
        std::cerr
            << "Could not read the Roblox Mach-O layout.\n";

        return 1;
    }

    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
    {
        std::cerr
            << "Could not open Mach-O file:\n"
            << "  "
            << executablePath
            << "\n";

        return 1;
    }

    input.seekg(0, std::ios::end);

    std::streamoff fileSize =
        input.tellg();

    if (fileSize <= 0)
    {
        std::cerr
            << "Mach-O file is empty or unreadable.\n";

        return 1;
    }

    input.seekg(0);

    std::vector<char> bytes(
        static_cast<size_t>(fileSize)
    );

    input.read(
        bytes.data(),
        fileSize
    );

    if (!input)
    {
        std::cerr
            << "Could not read the Mach-O file.\n";

        return 1;
    }

    std::cout
        << "\nAegisLab C-String Context Search\n"
        << "================================\n\n"
        << "File: "
        << executablePath
        << "\n"
        << "Search: "
        << searchText
        << "\n\n";

    size_t matches = 0;
    size_t shown = 0;
    constexpr size_t kMaxShown = 100;

    size_t i = 0;

    while (i < bytes.size())
    {
        while (i < bytes.size() &&
               !isPrintableCStringByte(
                   static_cast<unsigned char>(
                       bytes[i])))
        {
            ++i;
        }

        if (i >= bytes.size())
            break;

        size_t start = i;

        while (i < bytes.size() &&
               isPrintableCStringByte(
                   static_cast<unsigned char>(
                       bytes[i])))
        {
            ++i;
        }

        size_t end = i;

        // Require null termination to treat it as a C string.
        if (end >= bytes.size() ||
            bytes[end] != '\0')
        {
            ++i;
            continue;
        }

        if (end <= start)
        {
            ++i;
            continue;
        }

        std::string current(
            bytes.data() + start,
            end - start
        );

        size_t found =
            current.find(searchText);

        if (found !=
            std::string::npos)
        {
            ++matches;

            if (shown < kMaxShown)
            {
                ++shown;

                uint64_t matchFileOffset =
                    static_cast<uint64_t>(
                        start + found
                    );

                uint64_t preferredAddress = 0;
                uint64_t relativeOffset = 0;
                std::string location;

                bool mapped =
                    mapFileOffsetToMachLocation(
                        matchFileOffset,
                        preferredTextBase,
                        segments,
                        sections,
                        preferredAddress,
                        relativeOffset,
                        location
                    );

                std::cout
                    << "Match "
                    << matches
                    << "\n"
                    << "  C string:       "
                    << current
                    << "\n"
                    << "  String offset:  0x"
                    << std::hex
                    << static_cast<uint64_t>(
                        start
                    )
                    << "\n"
                    << "  Match offset:   0x"
                    << matchFileOffset
                    << std::dec
                    << "\n";

                if (mapped)
                {
                    std::cout
                        << "  Preferred addr: 0x"
                        << std::hex
                        << preferredAddress
                        << "\n"
                        << "  Relative:      +0x"
                        << relativeOffset
                        << std::dec
                        << "\n"
                        << "  Location:       "
                        << location
                        << "\n";
                }
                else
                {
                    std::cout
                        << "  Preferred addr: (not mapped)\n"
                        << "  Relative:       (not mapped)\n"
                        << "  Location:       (file offset is not inside a mapped section)\n";
                }

                std::cout << "\n";
            }
        }

        ++i;
    }

    std::cout
        << "Matches: "
        << matches
        << "\n";

    if (matches > kMaxShown)
    {
        std::cout
            << "Displayed: "
            << kMaxShown
            << " (output capped)\n";
    }

    std::cout << "\n";

    return 0;
}


static uint32_t readLittleEndianU32(
    const std::vector<char>& bytes,
    size_t offset)
{
    return
        static_cast<uint32_t>(
            static_cast<unsigned char>(
                bytes[offset]))
        |
        (static_cast<uint32_t>(
            static_cast<unsigned char>(
                bytes[offset + 1])) << 8)
        |
        (static_cast<uint32_t>(
            static_cast<unsigned char>(
                bytes[offset + 2])) << 16)
        |
        (static_cast<uint32_t>(
            static_cast<unsigned char>(
                bytes[offset + 3])) << 24);
}

static int64_t signExtend21(
    uint64_t value)
{
    constexpr uint64_t signBit =
        1ULL << 20;

    constexpr uint64_t mask =
        (1ULL << 21) - 1;

    value &= mask;

    if (value & signBit)
        value |= ~mask;

    return static_cast<int64_t>(value);
}

static bool decodeAdrp(
    uint32_t instruction,
    uint64_t instructionAddress,
    uint32_t& destinationRegister,
    uint64_t& targetPage)
{
    if ((instruction & 0x9F000000U) !=
        0x90000000U)
    {
        return false;
    }

    uint64_t immlo =
        (instruction >> 29) & 0x3;

    uint64_t immhi =
        (instruction >> 5) & 0x7FFFF;

    uint64_t combined =
        (immhi << 2) | immlo;

    int64_t signedPages =
        signExtend21(combined);

    int64_t pageDelta =
        signedPages << 12;

    uint64_t pcPage =
        instructionAddress &
        ~0xFFFULL;

    targetPage =
        static_cast<uint64_t>(
            static_cast<int64_t>(
                pcPage) +
            pageDelta
        );

    destinationRegister =
        instruction & 0x1F;

    return true;
}

static bool decodeAddImmediate64(
    uint32_t instruction,
    uint32_t& destinationRegister,
    uint32_t& sourceRegister,
    uint64_t& immediate)
{
    // ADD (immediate), 64-bit, without flags.
    if ((instruction & 0xFF000000U) !=
        0x91000000U)
    {
        return false;
    }

    uint64_t imm12 =
        (instruction >> 10) & 0xFFF;

    uint64_t shift =
        (instruction >> 22) & 0x1;

    immediate =
        shift
            ? (imm12 << 12)
            : imm12;

    sourceRegister =
        (instruction >> 5) & 0x1F;

    destinationRegister =
        instruction & 0x1F;

    return true;
}

static int runMachOXref(
    const std::string& executablePath,
    const std::string& offsetText)
{
    uint64_t relativeOffset = 0;

    if (!parseRelativeOffset(
            offsetText,
            relativeOffset))
    {
        std::cerr
            << "Invalid module-relative offset: "
            << offsetText
            << "\n\n"
            << "Example:\n"
            << "  ./build/AegisLab macho-xref +0x60a69f9\n";

        return 1;
    }

    uint64_t preferredTextBase = 0;
    std::vector<MachSegmentInfo> segments;
    std::vector<MachSectionInfo> sections;

    if (!loadMachSectionLayout(
            executablePath,
            preferredTextBase,
            segments,
            sections))
    {
        std::cerr
            << "Could not read the Roblox Mach-O layout.\n";

        return 1;
    }

    const MachSectionInfo* textSection =
        nullptr;

    for (const MachSectionInfo& section :
         sections)
    {
        if (section.segmentName ==
                "__TEXT" &&
            section.sectionName ==
                "__text")
        {
            textSection =
                &section;

            break;
        }
    }

    if (!textSection)
    {
        std::cerr
            << "Could not locate __TEXT::__text.\n";

        return 1;
    }

    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
    {
        std::cerr
            << "Could not open Mach-O file:\n"
            << "  "
            << executablePath
            << "\n";

        return 1;
    }

    input.seekg(
        0,
        std::ios::end
    );

    std::streamoff fileSize =
        input.tellg();

    if (fileSize <= 0)
    {
        std::cerr
            << "Mach-O file is empty or unreadable.\n";

        return 1;
    }

    input.seekg(0);

    std::vector<char> bytes(
        static_cast<size_t>(
            fileSize)
    );

    input.read(
        bytes.data(),
        fileSize
    );

    if (!input)
    {
        std::cerr
            << "Could not read the Mach-O file.\n";

        return 1;
    }

    uint64_t preferredTarget =
        preferredTextBase +
        relativeOffset;

    uint64_t targetPage =
        preferredTarget &
        ~0xFFFULL;

    std::cout
        << "\nAegisLab ARM64 Xref Search\n"
        << "==========================\n\n"
        << "File: "
        << executablePath
        << "\n"
        << std::hex
        << "Target relative:  +0x"
        << relativeOffset
        << "\n"
        << "Target preferred: 0x"
        << preferredTarget
        << "\n"
        << "Target page:      0x"
        << targetPage
        << std::dec
        << "\n"
        << "Target location:  "
        << describeMachLocation(
            preferredTarget,
            segments,
            sections
        )
        << "\n\n";

    const uint64_t textFileStart =
        textSection->fileOffset;

    const uint64_t textFileEnd =
        textFileStart +
        textSection->size;

    if (textFileEnd >
        static_cast<uint64_t>(
            bytes.size()))
    {
        std::cerr
            << "__TEXT::__text exceeds the file size.\n";

        return 1;
    }

    size_t exactMatches = 0;
    size_t pageCandidates = 0;

    for (uint64_t fileOffset =
             textFileStart;
         fileOffset + 4 <=
             textFileEnd;
         fileOffset += 4)
    {
        uint32_t instruction =
            readLittleEndianU32(
                bytes,
                static_cast<size_t>(
                    fileOffset)
            );

        uint64_t instructionAddress =
            textSection->vmaddr +
            (fileOffset -
             textFileStart);

        uint32_t adrpRegister = 0;
        uint64_t adrpTargetPage = 0;

        if (!decodeAdrp(
                instruction,
                instructionAddress,
                adrpRegister,
                adrpTargetPage))
        {
            continue;
        }

        if (adrpTargetPage !=
            targetPage)
        {
            continue;
        }

        ++pageCandidates;

        // ARM64 commonly materializes a static address with:
        //   ADRP Xn, page
        //   ADD  Xd, Xn, #pageOffset
        //
        // Search a short window so one unrelated instruction between
        // the pair does not make us miss a useful reference.
        constexpr uint64_t kLookAheadInstructions =
            6;

        for (uint64_t step = 1;
             step <=
                 kLookAheadInstructions;
             ++step)
        {
            uint64_t nextFileOffset =
                fileOffset +
                (step * 4);

            if (nextFileOffset + 4 >
                textFileEnd)
            {
                break;
            }

            uint32_t nextInstruction =
                readLittleEndianU32(
                    bytes,
                    static_cast<size_t>(
                        nextFileOffset)
                );

            uint32_t addDestination = 0;
            uint32_t addSource = 0;
            uint64_t addImmediate = 0;

            if (!decodeAddImmediate64(
                    nextInstruction,
                    addDestination,
                    addSource,
                    addImmediate))
            {
                continue;
            }

            if (addSource !=
                adrpRegister)
            {
                continue;
            }

            uint64_t materializedAddress =
                adrpTargetPage +
                addImmediate;

            if (materializedAddress !=
                preferredTarget)
            {
                continue;
            }

            ++exactMatches;

            uint64_t referenceRelative =
                instructionAddress -
                preferredTextBase;

            uint64_t addInstructionAddress =
                textSection->vmaddr +
                (nextFileOffset -
                 textFileStart);

            std::cout
                << "Xref "
                << exactMatches
                << "\n"
                << std::hex
                << "  ADRP preferred: 0x"
                << instructionAddress
                << "\n"
                << "  ADRP relative: +0x"
                << referenceRelative
                << "\n"
                << "  ADD preferred:  0x"
                << addInstructionAddress
                << "\n"
                << "  ADD distance:    +"
                << std::dec
                << step
                << " instruction";

            if (step != 1)
                std::cout << "s";

            std::cout
                << "\n"
                << std::hex
                << "  Register:        X"
                << adrpRegister
                << "\n"
                << "  Final address:   0x"
                << materializedAddress
                << "\n"
                << std::dec
                << "  Code location:   "
                << describeMachLocation(
                    instructionAddress,
                    segments,
                    sections
                )
                << "\n\n";

            break;
        }
    }

    std::cout
        << "Exact ADRP+ADD xrefs: "
        << exactMatches
        << "\n"
        << "ADRP page candidates: "
        << pageCandidates
        << "\n\n";

    if (exactMatches == 0 &&
        pageCandidates > 0)
    {
        std::cout
            << "The target page is referenced, but no exact ADRP+ADD "
            << "pair was found in the short instruction window.\n"
            << "That can happen when the compiler uses another ARM64 "
            << "addressing pattern.\n\n";
    }

    return 0;
}



static bool decodeLdrUnsignedImmediate(
    uint32_t instruction,
    uint32_t& destinationRegister,
    uint32_t& baseRegister,
    uint64_t& byteOffset,
    bool& is64Bit)
{
    // LDR (unsigned immediate), integer 64-bit.
    if ((instruction & 0xFFC00000U) ==
        0xF9400000U)
    {
        uint64_t imm12 =
            (instruction >> 10) & 0xFFF;

        destinationRegister =
            instruction & 0x1F;

        baseRegister =
            (instruction >> 5) & 0x1F;

        byteOffset =
            imm12 * 8ULL;

        is64Bit = true;

        return true;
    }

    // LDR (unsigned immediate), integer 32-bit.
    if ((instruction & 0xFFC00000U) ==
        0xB9400000U)
    {
        uint64_t imm12 =
            (instruction >> 10) & 0xFFF;

        destinationRegister =
            instruction & 0x1F;

        baseRegister =
            (instruction >> 5) & 0x1F;

        byteOffset =
            imm12 * 4ULL;

        is64Bit = false;

        return true;
    }

    return false;
}

static bool instructionDefinitelyClobbersRegister(
    uint32_t instruction,
    uint32_t registerNumber)
{
    uint32_t destination = 0;
    uint32_t source = 0;
    uint64_t immediate = 0;

    if (decodeAddImmediate64(
            instruction,
            destination,
            source,
            immediate))
    {
        return destination ==
            registerNumber;
    }

    uint32_t adrpDestination = 0;
    uint64_t adrpPage = 0;

    if (decodeAdrp(
            instruction,
            0,
            adrpDestination,
            adrpPage))
    {
        return adrpDestination ==
            registerNumber;
    }

    uint32_t ldrDestination = 0;
    uint32_t ldrBase = 0;
    uint64_t ldrOffset = 0;
    bool ldr64 = false;

    if (decodeLdrUnsignedImmediate(
            instruction,
            ldrDestination,
            ldrBase,
            ldrOffset,
            ldr64))
    {
        return ldrDestination ==
            registerNumber;
    }

    // MOV Xd, Xn is an alias of ORR Xd, XZR, Xn.
    if ((instruction &
         0xFFE0FFE0U) ==
        0xAA0003E0U)
    {
        destination =
            instruction & 0x1F;

        return destination ==
            registerNumber;
    }

    // MOV Wd, Wn.
    if ((instruction &
         0xFFE0FFE0U) ==
        0x2A0003E0U)
    {
        destination =
            instruction & 0x1F;

        return destination ==
            registerNumber;
    }

    return false;
}

static int runMachODataXrefs(
    const std::string& executablePath,
    const std::string& targetOffsetText)
{
    uint64_t targetRelativeOffset = 0;

    if (!parseRelativeOffset(
            targetOffsetText,
            targetRelativeOffset))
    {
        std::cerr
            << "Invalid module-relative target offset: "
            << targetOffsetText
            << "\n\n"
            << "Example:\n"
            << "  ./build/AegisLab macho-data-xrefs +0x662ff00\n";

        return 1;
    }

    uint64_t preferredTextBase = 0;
    std::vector<MachSegmentInfo> segments;
    std::vector<MachSectionInfo> sections;

    if (!loadMachSectionLayout(
            executablePath,
            preferredTextBase,
            segments,
            sections))
    {
        std::cerr
            << "Could not read the Roblox Mach-O layout.\n";

        return 1;
    }

    const MachSectionInfo* textSection =
        nullptr;

    for (const MachSectionInfo& section :
         sections)
    {
        if (section.segmentName ==
                "__TEXT" &&
            section.sectionName ==
                "__text")
        {
            textSection =
                &section;

            break;
        }
    }

    if (!textSection)
    {
        std::cerr
            << "Could not locate __TEXT::__text.\n";

        return 1;
    }

    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
    {
        std::cerr
            << "Could not open Mach-O file:\n"
            << "  "
            << executablePath
            << "\n";

        return 1;
    }

    input.seekg(0, std::ios::end);

    std::streamoff fileSize =
        input.tellg();

    if (fileSize <= 0)
    {
        std::cerr
            << "Mach-O file is empty or unreadable.\n";

        return 1;
    }

    input.seekg(0);

    std::vector<char> bytes(
        static_cast<size_t>(
            fileSize)
    );

    input.read(
        bytes.data(),
        fileSize
    );

    if (!input)
    {
        std::cerr
            << "Could not read the Mach-O file.\n";

        return 1;
    }

    const uint64_t targetPreferredAddress =
        preferredTextBase +
        targetRelativeOffset;

    const uint64_t targetPage =
        targetPreferredAddress &
        ~0xFFFULL;

    const uint64_t textFileStart =
        textSection->fileOffset;

    uint64_t textFileEnd =
        textSection->fileOffset +
        textSection->size;

    textFileEnd =
        std::min<uint64_t>(
            textFileEnd,
            bytes.size()
        );

    struct DataXref
    {
        std::string kind;
        uint64_t adrpAddress = 0;
        uint64_t secondAddress = 0;
        uint64_t finalAddress = 0;
        uint32_t pageRegister = 0;
        uint32_t destinationRegister = 0;
        uint64_t distanceInstructions = 0;
    };

    std::vector<DataXref> matches;
    size_t pageCandidates = 0;

    constexpr uint64_t kLookAheadInstructions =
        8;

    for (uint64_t fileOffset =
             textFileStart;
         fileOffset + 4 <=
             textFileEnd;
         fileOffset += 4)
    {
        uint32_t instruction =
            readLittleEndianU32(
                bytes,
                static_cast<size_t>(
                    fileOffset)
            );

        uint64_t instructionAddress =
            textSection->vmaddr +
            (fileOffset -
             textFileStart);

        uint32_t pageRegister = 0;
        uint64_t pageAddress = 0;

        if (!decodeAdrp(
                instruction,
                instructionAddress,
                pageRegister,
                pageAddress))
        {
            continue;
        }

        if (pageAddress !=
            targetPage)
        {
            continue;
        }

        ++pageCandidates;

        for (uint64_t step = 1;
             step <=
                 kLookAheadInstructions;
             ++step)
        {
            uint64_t secondFileOffset =
                fileOffset +
                (step * 4);

            if (secondFileOffset + 4 >
                textFileEnd)
            {
                break;
            }

            uint32_t secondInstruction =
                readLittleEndianU32(
                    bytes,
                    static_cast<size_t>(
                        secondFileOffset)
                );

            uint64_t secondAddress =
                textSection->vmaddr +
                (secondFileOffset -
                 textFileStart);

            uint32_t addDestination = 0;
            uint32_t addSource = 0;
            uint64_t addImmediate = 0;

            if (decodeAddImmediate64(
                    secondInstruction,
                    addDestination,
                    addSource,
                    addImmediate) &&
                addSource ==
                    pageRegister)
            {
                uint64_t finalAddress =
                    pageAddress +
                    addImmediate;

                if (finalAddress ==
                    targetPreferredAddress)
                {
                    DataXref match;
                    match.kind =
                        "ADRP+ADD";
                    match.adrpAddress =
                        instructionAddress;
                    match.secondAddress =
                        secondAddress;
                    match.finalAddress =
                        finalAddress;
                    match.pageRegister =
                        pageRegister;
                    match.destinationRegister =
                        addDestination;
                    match.distanceInstructions =
                        step;

                    matches.push_back(
                        match
                    );

                    break;
                }
            }

            uint32_t ldrDestination = 0;
            uint32_t ldrBase = 0;
            uint64_t ldrByteOffset = 0;
            bool ldr64 = false;

            if (decodeLdrUnsignedImmediate(
                    secondInstruction,
                    ldrDestination,
                    ldrBase,
                    ldrByteOffset,
                    ldr64) &&
                ldrBase ==
                    pageRegister)
            {
                uint64_t referencedAddress =
                    pageAddress +
                    ldrByteOffset;

                if (referencedAddress ==
                    targetPreferredAddress)
                {
                    DataXref match;
                    match.kind =
                        ldr64
                            ? "ADRP+LDR X"
                            : "ADRP+LDR W";
                    match.adrpAddress =
                        instructionAddress;
                    match.secondAddress =
                        secondAddress;
                    match.finalAddress =
                        referencedAddress;
                    match.pageRegister =
                        pageRegister;
                    match.destinationRegister =
                        ldrDestination;
                    match.distanceInstructions =
                        step;

                    matches.push_back(
                        match
                    );

                    break;
                }
            }

            // Once the ADRP destination register is overwritten, later
            // instructions can no longer be part of this address-building
            // sequence. Stop this candidate here.
            if (instructionDefinitelyClobbersRegister(
                    secondInstruction,
                    pageRegister))
            {
                break;
            }
        }
    }

    std::cout
        << "\nAegisLab ARM64 Data Xrefs\n"
        << "=========================\n\n"
        << "File: "
        << executablePath
        << "\n"
        << std::hex
        << "Target relative:  +0x"
        << targetRelativeOffset
        << "\n"
        << "Target preferred: 0x"
        << targetPreferredAddress
        << "\n"
        << "Target page:      0x"
        << targetPage
        << "\n"
        << std::dec
        << "Target location:  "
        << describeMachLocation(
            targetPreferredAddress,
            segments,
            sections
        )
        << "\n\n";

    if (matches.empty())
    {
        std::cout
            << "No exact ADRP+ADD or ADRP+LDR code references found.\n"
            << "ADRP references to the target page: "
            << pageCandidates
            << "\n\n"
            << "This scanner follows a short instruction window and covers the common\n"
            << "ARM64 static-address forms ADRP+ADD and ADRP+LDR (unsigned immediate).\n"
            << "A zero result can still mean the address is reached through another\n"
            << "instruction form, a relocated/fixup pointer, or an indirect table.\n\n";

        return 0;
    }

    for (size_t i = 0;
         i < matches.size();
         ++i)
    {
        const DataXref& match =
            matches[i];

        uint64_t relativeAddress =
            match.adrpAddress -
            preferredTextBase;

        std::cout
            << "Xref "
            << (i + 1)
            << ":\n"
            << "  Pattern:         "
            << match.kind
            << "\n"
            << std::hex
            << "  ADRP preferred:  0x"
            << match.adrpAddress
            << "\n"
            << "  ADRP relative:   +0x"
            << relativeAddress
            << "\n"
            << "  Second instr:    0x"
            << match.secondAddress
            << "\n"
            << "  Final address:   0x"
            << match.finalAddress
            << "\n"
            << std::dec
            << "  Page register:   X"
            << match.pageRegister
            << "\n"
            << "  Destination:     "
            << ((match.kind ==
                     "ADRP+LDR W")
                    ? "W"
                    : "X")
            << match.destinationRegister
            << "\n"
            << "  Distance:        "
            << match.distanceInstructions
            << " instruction";

        if (match.distanceInstructions != 1)
            std::cout << "s";

        std::cout
            << "\n"
            << "  Code location:   "
            << describeMachLocation(
                match.adrpAddress,
                segments,
                sections
            )
            << "\n"
            << "  Inspect with:    ./build/AegisLab macho-code +0x"
            << std::hex
            << relativeAddress
            << std::dec
            << " 40\n\n";
    }

    std::cout
        << "Exact data xrefs: "
        << matches.size()
        << "\n"
        << "ADRP page candidates: "
        << pageCandidates
        << "\n\n";

    return 0;
}



static int runMachODataNear(
    const std::string& executablePath,
    const std::string& targetOffsetText,
    const std::string& radiusText)
{
    uint64_t targetRelativeOffset = 0;

    if (!parseRelativeOffset(
            targetOffsetText,
            targetRelativeOffset))
    {
        std::cerr
            << "Invalid module-relative target offset: "
            << targetOffsetText
            << "\n\n"
            << "Example:\n"
            << "  ./build/AegisLab macho-data-near +0x662ff00 0x100\n";

        return 1;
    }

    uint64_t radius = 0x100;

    if (!radiusText.empty() &&
        !parseRelativeOffset(
            radiusText,
            radius))
    {
        std::cerr
            << "Invalid radius: "
            << radiusText
            << "\n\n"
            << "Example:\n"
            << "  ./build/AegisLab macho-data-near +0x662ff00 0x100\n";

        return 1;
    }

    uint64_t preferredTextBase = 0;
    std::vector<MachSegmentInfo> segments;
    std::vector<MachSectionInfo> sections;

    if (!loadMachSectionLayout(
            executablePath,
            preferredTextBase,
            segments,
            sections))
    {
        std::cerr
            << "Could not read the Roblox Mach-O layout.\n";

        return 1;
    }

    const MachSectionInfo* textSection =
        nullptr;

    for (const MachSectionInfo& section :
         sections)
    {
        if (section.segmentName ==
                "__TEXT" &&
            section.sectionName ==
                "__text")
        {
            textSection =
                &section;

            break;
        }
    }

    if (!textSection)
    {
        std::cerr
            << "Could not locate __TEXT::__text.\n";

        return 1;
    }

    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
    {
        std::cerr
            << "Could not open Mach-O file:\n"
            << "  "
            << executablePath
            << "\n";

        return 1;
    }

    input.seekg(0, std::ios::end);

    std::streamoff fileSize =
        input.tellg();

    if (fileSize <= 0)
    {
        std::cerr
            << "Mach-O file is empty or unreadable.\n";

        return 1;
    }

    input.seekg(0);

    std::vector<char> bytes(
        static_cast<size_t>(
            fileSize)
    );

    input.read(
        bytes.data(),
        fileSize
    );

    if (!input)
    {
        std::cerr
            << "Could not read the Mach-O file.\n";

        return 1;
    }

    const uint64_t targetPreferredAddress =
        preferredTextBase +
        targetRelativeOffset;

    const uint64_t targetPage =
        targetPreferredAddress &
        ~0xFFFULL;

    const uint64_t lowerBound =
        targetPreferredAddress >= radius
            ? targetPreferredAddress - radius
            : 0;

    const uint64_t upperBound =
        (UINT64_MAX - targetPreferredAddress <
         radius)
            ? UINT64_MAX
            : targetPreferredAddress + radius;

    const uint64_t textFileStart =
        textSection->fileOffset;

    uint64_t textFileEnd =
        textSection->fileOffset +
        textSection->size;

    textFileEnd =
        std::min<uint64_t>(
            textFileEnd,
            bytes.size()
        );

    struct NearbyXref
    {
        std::string kind;
        uint64_t adrpAddress = 0;
        uint64_t secondAddress = 0;
        uint64_t finalAddress = 0;
        uint32_t pageRegister = 0;
        uint32_t destinationRegister = 0;
        uint64_t distanceInstructions = 0;
    };

    std::vector<NearbyXref> matches;
    size_t pageCandidates = 0;

    constexpr uint64_t kLookAheadInstructions =
        12;

    for (uint64_t fileOffset =
             textFileStart;
         fileOffset + 4 <=
             textFileEnd;
         fileOffset += 4)
    {
        uint32_t instruction =
            readLittleEndianU32(
                bytes,
                static_cast<size_t>(
                    fileOffset)
            );

        uint64_t instructionAddress =
            textSection->vmaddr +
            (fileOffset -
             textFileStart);

        uint32_t pageRegister = 0;
        uint64_t pageAddress = 0;

        if (!decodeAdrp(
                instruction,
                instructionAddress,
                pageRegister,
                pageAddress))
        {
            continue;
        }

        if (pageAddress !=
            targetPage)
        {
            continue;
        }

        ++pageCandidates;

        for (uint64_t step = 1;
             step <=
                 kLookAheadInstructions;
             ++step)
        {
            uint64_t secondFileOffset =
                fileOffset +
                (step * 4);

            if (secondFileOffset + 4 >
                textFileEnd)
            {
                break;
            }

            uint32_t secondInstruction =
                readLittleEndianU32(
                    bytes,
                    static_cast<size_t>(
                        secondFileOffset)
                );

            uint64_t secondAddress =
                textSection->vmaddr +
                (secondFileOffset -
                 textFileStart);

            bool matchedThisInstruction =
                false;

            uint32_t addDestination = 0;
            uint32_t addSource = 0;
            uint64_t addImmediate = 0;

            if (decodeAddImmediate64(
                    secondInstruction,
                    addDestination,
                    addSource,
                    addImmediate) &&
                addSource ==
                    pageRegister)
            {
                uint64_t finalAddress =
                    pageAddress +
                    addImmediate;

                if (finalAddress >=
                        lowerBound &&
                    finalAddress <=
                        upperBound)
                {
                    NearbyXref match;
                    match.kind =
                        "ADRP+ADD";
                    match.adrpAddress =
                        instructionAddress;
                    match.secondAddress =
                        secondAddress;
                    match.finalAddress =
                        finalAddress;
                    match.pageRegister =
                        pageRegister;
                    match.destinationRegister =
                        addDestination;
                    match.distanceInstructions =
                        step;

                    matches.push_back(
                        match
                    );

                    matchedThisInstruction =
                        true;
                }

                // If ADD writes back into the ADRP register, the original
                // page value is gone after this instruction.
                if (addDestination ==
                    pageRegister)
                {
                    break;
                }
            }

            uint32_t ldrDestination = 0;
            uint32_t ldrBase = 0;
            uint64_t ldrByteOffset = 0;
            bool ldr64 = false;

            if (decodeLdrUnsignedImmediate(
                    secondInstruction,
                    ldrDestination,
                    ldrBase,
                    ldrByteOffset,
                    ldr64) &&
                ldrBase ==
                    pageRegister)
            {
                uint64_t referencedAddress =
                    pageAddress +
                    ldrByteOffset;

                if (referencedAddress >=
                        lowerBound &&
                    referencedAddress <=
                        upperBound)
                {
                    NearbyXref match;
                    match.kind =
                        ldr64
                            ? "ADRP+LDR X"
                            : "ADRP+LDR W";
                    match.adrpAddress =
                        instructionAddress;
                    match.secondAddress =
                        secondAddress;
                    match.finalAddress =
                        referencedAddress;
                    match.pageRegister =
                        pageRegister;
                    match.destinationRegister =
                        ldrDestination;
                    match.distanceInstructions =
                        step;

                    matches.push_back(
                        match
                    );

                    matchedThisInstruction =
                        true;
                }

                if (ldrDestination ==
                    pageRegister)
                {
                    break;
                }
            }

            if (!matchedThisInstruction &&
                instructionDefinitelyClobbersRegister(
                    secondInstruction,
                    pageRegister))
            {
                break;
            }
        }
    }

    auto addressDistance =
        [&](uint64_t address)
        {
            return address >=
                    targetPreferredAddress
                ? address -
                    targetPreferredAddress
                : targetPreferredAddress -
                    address;
        };

    std::sort(
        matches.begin(),
        matches.end(),
        [&](const NearbyXref& a,
            const NearbyXref& b)
        {
            uint64_t distanceA =
                addressDistance(
                    a.finalAddress);

            uint64_t distanceB =
                addressDistance(
                    b.finalAddress);

            if (distanceA !=
                distanceB)
            {
                return distanceA <
                    distanceB;
            }

            if (a.finalAddress !=
                b.finalAddress)
            {
                return a.finalAddress <
                    b.finalAddress;
            }

            return a.adrpAddress <
                b.adrpAddress;
        }
    );

    std::cout
        << "\nAegisLab Nearby ARM64 Data Xrefs\n"
        << "================================\n\n"
        << "File: "
        << executablePath
        << "\n"
        << std::hex
        << "Target relative:  +0x"
        << targetRelativeOffset
        << "\n"
        << "Target preferred: 0x"
        << targetPreferredAddress
        << "\n"
        << "Target page:      0x"
        << targetPage
        << "\n"
        << "Radius:           0x"
        << radius
        << "\n"
        << "Search range:     0x"
        << lowerBound
        << " - 0x"
        << upperBound
        << "\n"
        << std::dec
        << "Target location:  "
        << describeMachLocation(
            targetPreferredAddress,
            segments,
            sections
        )
        << "\n\n";

    if (matches.empty())
    {
        std::cout
            << "No ADRP+ADD or ADRP+LDR references resolved within the requested range.\n"
            << "ADRP references to the target page: "
            << pageCandidates
            << "\n\n"
            << "This still does not rule out dyld fixups, register-offset loads,\n"
            << "multi-stage arithmetic, or other indirect access patterns.\n\n";

        return 0;
    }

    for (size_t i = 0;
         i < matches.size();
         ++i)
    {
        const NearbyXref& match =
            matches[i];

        uint64_t relativeAddress =
            match.adrpAddress -
            preferredTextBase;

        bool above =
            match.finalAddress >=
            targetPreferredAddress;

        uint64_t delta =
            addressDistance(
                match.finalAddress);

        std::cout
            << "Xref "
            << (i + 1)
            << ":\n"
            << "  Pattern:         "
            << match.kind
            << "\n"
            << std::hex
            << "  Resolved addr:   0x"
            << match.finalAddress
            << "\n"
            << "  Relative target: +0x"
            << (match.finalAddress -
                preferredTextBase)
            << "\n"
            << "  Delta from base: "
            << (above ? "+" : "-")
            << "0x"
            << delta
            << "\n"
            << "  ADRP preferred:  0x"
            << match.adrpAddress
            << "\n"
            << "  ADRP relative:   +0x"
            << relativeAddress
            << "\n"
            << "  Second instr:    0x"
            << match.secondAddress
            << "\n"
            << std::dec
            << "  Page register:   X"
            << match.pageRegister
            << "\n"
            << "  Destination:     "
            << ((match.kind ==
                     "ADRP+LDR W")
                    ? "W"
                    : "X")
            << match.destinationRegister
            << "\n"
            << "  Distance:        "
            << match.distanceInstructions
            << " instruction";

        if (match.distanceInstructions != 1)
            std::cout << "s";

        std::cout
            << "\n"
            << "  Data location:   "
            << describeMachLocation(
                match.finalAddress,
                segments,
                sections
            )
            << "\n"
            << "  Code location:   "
            << describeMachLocation(
                match.adrpAddress,
                segments,
                sections
            )
            << "\n"
            << "  Inspect with:    ./build/AegisLab macho-code +0x"
            << std::hex
            << relativeAddress
            << std::dec
            << " 40\n\n";
    }

    std::cout
        << "Nearby data xrefs: "
        << matches.size()
        << "\n"
        << "ADRP page candidates: "
        << pageCandidates
        << "\n\n";

    return 0;
}


static int64_t signExtend26(
    uint64_t value)
{
    constexpr uint64_t signBit =
        1ULL << 25;

    constexpr uint64_t mask =
        (1ULL << 26) - 1;

    value &= mask;

    if (value & signBit)
        value |= ~mask;

    return static_cast<int64_t>(value);
}

static int64_t signExtend19(
    uint64_t value)
{
    constexpr uint64_t signBit =
        1ULL << 18;

    constexpr uint64_t mask =
        (1ULL << 19) - 1;

    value &= mask;

    if (value & signBit)
        value |= ~mask;

    return static_cast<int64_t>(value);
}


struct MachSymbolTableInfo
{
    uint32_t symbolOffset = 0;
    uint32_t symbolCount = 0;
    uint32_t stringOffset = 0;
    uint32_t stringSize = 0;
    uint32_t indirectOffset = 0;
    uint32_t indirectCount = 0;
    bool hasSymtab = false;
    bool hasDysymtab = false;
};

static bool loadMachSymbolTableInfo(
    const std::string& executablePath,
    MachSymbolTableInfo& info)
{
    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
        return false;

    mach_header_64 header{};

    input.read(
        reinterpret_cast<char*>(&header),
        sizeof(header)
    );

    if (!input ||
        header.magic != MH_MAGIC_64)
    {
        return false;
    }

    std::streamoff commandOffset =
        static_cast<std::streamoff>(
            sizeof(mach_header_64)
        );

    for (uint32_t i = 0;
         i < header.ncmds;
         ++i)
    {
        input.seekg(commandOffset);

        load_command command{};

        input.read(
            reinterpret_cast<char*>(&command),
            sizeof(command)
        );

        if (!input ||
            command.cmdsize <
                sizeof(load_command))
        {
            return false;
        }

        if (command.cmd == LC_SYMTAB)
        {
            input.seekg(commandOffset);

            symtab_command symtab{};

            input.read(
                reinterpret_cast<char*>(&symtab),
                sizeof(symtab)
            );

            if (!input)
                return false;

            info.symbolOffset =
                symtab.symoff;

            info.symbolCount =
                symtab.nsyms;

            info.stringOffset =
                symtab.stroff;

            info.stringSize =
                symtab.strsize;

            info.hasSymtab =
                true;
        }
        else if (command.cmd == LC_DYSYMTAB)
        {
            input.seekg(commandOffset);

            dysymtab_command dysymtab{};

            input.read(
                reinterpret_cast<char*>(&dysymtab),
                sizeof(dysymtab)
            );

            if (!input)
                return false;

            info.indirectOffset =
                dysymtab.indirectsymoff;

            info.indirectCount =
                dysymtab.nindirectsyms;

            info.hasDysymtab =
                true;
        }

        commandOffset +=
            static_cast<std::streamoff>(
                command.cmdsize
            );
    }

    return
        info.hasSymtab &&
        info.hasDysymtab;
}

static std::string readMachString(
    const std::vector<char>& bytes,
    uint64_t offset,
    uint64_t limit)
{
    if (offset >= bytes.size() ||
        offset >= limit)
    {
        return "";
    }

    size_t end =
        static_cast<size_t>(offset);

    size_t maxEnd =
        static_cast<size_t>(
            std::min<uint64_t>(
                limit,
                bytes.size()
            )
        );

    while (end < maxEnd &&
           bytes[end] != '\0')
    {
        ++end;
    }

    if (end <= offset)
        return "";

    return std::string(
        bytes.data() +
            static_cast<size_t>(offset),
        end -
            static_cast<size_t>(offset)
    );
}

static bool loadMachStubSymbols(
    const std::string& executablePath,
    const std::vector<MachSectionInfo>& sections,
    std::unordered_map<uint64_t, std::string>& stubSymbols)
{
    MachSymbolTableInfo info;

    if (!loadMachSymbolTableInfo(
            executablePath,
            info))
    {
        return false;
    }

    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
        return false;

    input.seekg(0, std::ios::end);

    std::streamoff fileSize =
        input.tellg();

    if (fileSize <= 0)
        return false;

    input.seekg(0);

    std::vector<char> bytes(
        static_cast<size_t>(fileSize)
    );

    input.read(
        bytes.data(),
        fileSize
    );

    if (!input)
        return false;

    uint64_t symbolTableEnd =
        static_cast<uint64_t>(
            info.symbolOffset) +
        static_cast<uint64_t>(
            info.symbolCount) *
        sizeof(nlist_64);

    uint64_t stringTableEnd =
        static_cast<uint64_t>(
            info.stringOffset) +
        info.stringSize;

    uint64_t indirectTableEnd =
        static_cast<uint64_t>(
            info.indirectOffset) +
        static_cast<uint64_t>(
            info.indirectCount) *
        sizeof(uint32_t);

    if (symbolTableEnd >
            bytes.size() ||
        stringTableEnd >
            bytes.size() ||
        indirectTableEnd >
            bytes.size())
    {
        return false;
    }

    std::vector<nlist_64> symbols(
        info.symbolCount
    );

    for (uint32_t i = 0;
         i < info.symbolCount;
         ++i)
    {
        size_t offset =
            static_cast<size_t>(
                info.symbolOffset) +
            static_cast<size_t>(i) *
                sizeof(nlist_64);

        std::memcpy(
            &symbols[i],
            bytes.data() + offset,
            sizeof(nlist_64)
        );
    }

    std::vector<uint32_t> indirect(
        info.indirectCount
    );

    for (uint32_t i = 0;
         i < info.indirectCount;
         ++i)
    {
        size_t offset =
            static_cast<size_t>(
                info.indirectOffset) +
            static_cast<size_t>(i) *
                sizeof(uint32_t);

        std::memcpy(
            &indirect[i],
            bytes.data() + offset,
            sizeof(uint32_t)
        );
    }

    constexpr uint32_t kIndirectLocal =
        INDIRECT_SYMBOL_LOCAL;

    constexpr uint32_t kIndirectAbsolute =
        INDIRECT_SYMBOL_ABS;

    for (const MachSectionInfo& section :
         sections)
    {
        uint32_t sectionType =
            section.flags &
            SECTION_TYPE;

        if (sectionType !=
            S_SYMBOL_STUBS)
        {
            continue;
        }

        uint32_t stubSize =
            section.reserved2;

        if (stubSize == 0)
            continue;

        uint64_t stubCount =
            section.size /
            stubSize;

        for (uint64_t i = 0;
             i < stubCount;
             ++i)
        {
            uint64_t indirectIndex64 =
                static_cast<uint64_t>(
                    section.reserved1) +
                i;

            if (indirectIndex64 >=
                indirect.size())
            {
                break;
            }

            uint32_t symbolIndex =
                indirect[
                    static_cast<size_t>(
                        indirectIndex64)
                ];

            if ((symbolIndex &
                 kIndirectLocal) ||
                (symbolIndex &
                 kIndirectAbsolute))
            {
                continue;
            }

            if (symbolIndex >=
                symbols.size())
            {
                continue;
            }

            const nlist_64& symbol =
                symbols[symbolIndex];

            uint32_t stringIndex =
                symbol.n_un.n_strx;

            if (stringIndex >=
                info.stringSize)
            {
                continue;
            }

            uint64_t stringOffset =
                static_cast<uint64_t>(
                    info.stringOffset) +
                stringIndex;

            std::string name =
                readMachString(
                    bytes,
                    stringOffset,
                    stringTableEnd
                );

            if (name.empty())
                continue;

            uint64_t stubAddress =
                section.vmaddr +
                (i * stubSize);

            stubSymbols[
                stubAddress
            ] = name;
        }
    }

    return !stubSymbols.empty();
}


static int64_t signExtend14(
    uint64_t value)
{
    constexpr uint64_t signBit =
        1ULL << 13;

    constexpr uint64_t mask =
        (1ULL << 14) - 1;

    value &= mask;

    if (value & signBit)
        value |= ~mask;

    return static_cast<int64_t>(value);
}

static int64_t signExtend9(
    uint64_t value)
{
    constexpr uint64_t signBit =
        1ULL << 8;

    constexpr uint64_t mask =
        (1ULL << 9) - 1;

    value &= mask;

    if (value & signBit)
        value |= ~mask;

    return static_cast<int64_t>(value);
}

static std::string arm64RegisterName(
    uint32_t reg,
    bool is64Bit,
    bool spAllowed = false)
{
    std::ostringstream out;

    if (reg == 31)
    {
        if (spAllowed)
            return is64Bit ? "SP" : "WSP";

        return is64Bit ? "XZR" : "WZR";
    }

    out
        << (is64Bit ? "X" : "W")
        << reg;

    return out.str();
}


static std::string decodeArm64Summary(
    uint32_t instruction,
    uint64_t instructionAddress,
    const std::vector<MachSegmentInfo>& segments,
    const std::vector<MachSectionInfo>& sections,
    const std::unordered_map<uint64_t, std::string>& stubSymbols)
{
    std::ostringstream out;

    // MOV register alias for ORR Rd, XZR/WZR, Rm.
    if ((instruction & 0xFFE0FFE0U) ==
            0xAA0003E0U ||
        (instruction & 0xFFE0FFE0U) ==
            0x2A0003E0U)
    {
        bool is64Bit =
            (instruction & 0x80000000U) != 0;

        uint32_t rm =
            (instruction >> 16) & 0x1F;

        uint32_t rd =
            instruction & 0x1F;

        out
            << "MOV "
            << arm64RegisterName(
                rd,
                is64Bit)
            << ", "
            << arm64RegisterName(
                rm,
                is64Bit);

        return out.str();
    }

    // BLR Xn.
    if ((instruction & 0xFFFFFC1FU) ==
        0xD63F0000U)
    {
        uint32_t rn =
            (instruction >> 5) & 0x1F;

        out
            << "BLR X"
            << rn;

        return out.str();
    }

    // BR Xn.
    if ((instruction & 0xFFFFFC1FU) ==
        0xD61F0000U)
    {
        uint32_t rn =
            (instruction >> 5) & 0x1F;

        out
            << "BR X"
            << rn;

        return out.str();
    }

    // CBZ / CBNZ, 32- and 64-bit.
    if ((instruction & 0x7E000000U) ==
        0x34000000U)
    {
        bool is64Bit =
            (instruction &
             0x80000000U) != 0;

        bool nonZero =
            (instruction &
             0x01000000U) != 0;

        uint64_t imm19 =
            (instruction >> 5) &
            0x7FFFFU;

        int64_t displacement =
            signExtend19(imm19) << 2;

        uint64_t target =
            static_cast<uint64_t>(
                static_cast<int64_t>(
                    instructionAddress) +
                displacement
            );

        uint32_t rt =
            instruction & 0x1F;

        out
            << (nonZero
                    ? "CBNZ "
                    : "CBZ ")
            << arm64RegisterName(
                rt,
                is64Bit)
            << ", 0x"
            << std::hex
            << target
            << "  ["
            << describeMachLocation(
                target,
                segments,
                sections
            )
            << "]";

        return out.str();
    }

    // TBZ / TBNZ.
    if ((instruction & 0x7E000000U) ==
        0x36000000U)
    {
        bool nonZero =
            (instruction &
             0x01000000U) != 0;

        uint32_t bit =
            ((instruction >> 31) & 0x1)
                << 5;

        bit |=
            (instruction >> 19) &
            0x1F;

        uint64_t imm14 =
            (instruction >> 5) &
            0x3FFFU;

        int64_t displacement =
            signExtend14(imm14) << 2;

        uint64_t target =
            static_cast<uint64_t>(
                static_cast<int64_t>(
                    instructionAddress) +
                displacement
            );

        uint32_t rt =
            instruction & 0x1F;

        out
            << (nonZero
                    ? "TBNZ "
                    : "TBZ ")
            << arm64RegisterName(
                rt,
                true)
            << ", #"
            << std::dec
            << bit
            << ", 0x"
            << std::hex
            << target
            << "  ["
            << describeMachLocation(
                target,
                segments,
                sections
            )
            << "]";

        return out.str();
    }

    // LDR/STR unsigned immediate for common integer forms.
    {
        uint32_t top =
            instruction &
            0xFFC00000U;

        bool isLoad = false;
        bool isStore = false;
        bool is64Bit = false;
        uint32_t scale = 0;

        if (top == 0xF9400000U)
        {
            isLoad = true;
            is64Bit = true;
            scale = 3;
        }
        else if (top == 0xF9000000U)
        {
            isStore = true;
            is64Bit = true;
            scale = 3;
        }
        else if (top == 0xB9400000U)
        {
            isLoad = true;
            is64Bit = false;
            scale = 2;
        }
        else if (top == 0xB9000000U)
        {
            isStore = true;
            is64Bit = false;
            scale = 2;
        }
        else if (top == 0x39400000U)
        {
            isLoad = true;
            is64Bit = false;
            scale = 0;
        }
        else if (top == 0x39000000U)
        {
            isStore = true;
            is64Bit = false;
            scale = 0;
        }

        if (isLoad || isStore)
        {
            uint32_t imm12 =
                (instruction >> 10) &
                0xFFF;

            uint64_t byteOffset =
                static_cast<uint64_t>(
                    imm12) << scale;

            uint32_t rn =
                (instruction >> 5) &
                0x1F;

            uint32_t rt =
                instruction &
                0x1F;

            out
                << (isLoad
                        ? "LDR "
                        : "STR ")
                << arm64RegisterName(
                    rt,
                    is64Bit)
                << ", ["
                << arm64RegisterName(
                    rn,
                    true,
                    true);

            if (byteOffset != 0)
            {
                out
                    << ", #0x"
                    << std::hex
                    << byteOffset;
            }

            out
                << "]";

            return out.str();
        }
    }

    // LDUR/STUR common integer forms.
    {
        uint32_t masked =
            instruction &
            0xFFE00C00U;

        bool isLoad = false;
        bool isStore = false;
        bool is64Bit = false;

        if (masked == 0xF8400000U)
        {
            isLoad = true;
            is64Bit = true;
        }
        else if (masked == 0xF8000000U)
        {
            isStore = true;
            is64Bit = true;
        }
        else if (masked == 0xB8400000U)
        {
            isLoad = true;
            is64Bit = false;
        }
        else if (masked == 0xB8000000U)
        {
            isStore = true;
            is64Bit = false;
        }

        if (isLoad || isStore)
        {
            uint32_t imm9 =
                (instruction >> 12) &
                0x1FF;

            int64_t offset =
                signExtend9(imm9);

            uint32_t rn =
                (instruction >> 5) &
                0x1F;

            uint32_t rt =
                instruction &
                0x1F;

            out
                << (isLoad
                        ? "LDUR "
                        : "STUR ")
                << arm64RegisterName(
                    rt,
                    is64Bit)
                << ", ["
                << arm64RegisterName(
                    rn,
                    true,
                    true);

            if (offset != 0)
            {
                out
                    << ", #"
                    << std::dec
                    << offset;
            }

            out
                << "]";

            return out.str();
        }
    }

    // LDP/STP common 64-bit pair forms, including pre/post-index.
    {
        uint32_t op =
            instruction &
            0xFFC00000U;

        bool pair = false;
        bool load = false;
        bool preIndex = false;
        bool postIndex = false;

        if (op == 0xA9400000U)
        {
            pair = true;
            load = true;
        }
        else if (op == 0xA9000000U)
        {
            pair = true;
            load = false;
        }
        else if (op == 0xA9C00000U)
        {
            pair = true;
            load = true;
            preIndex = true;
        }
        else if (op == 0xA9800000U)
        {
            pair = true;
            load = false;
            preIndex = true;
        }
        else if (op == 0xA8C00000U)
        {
            pair = true;
            load = true;
            postIndex = true;
        }
        else if (op == 0xA8800000U)
        {
            pair = true;
            load = false;
            postIndex = true;
        }

        if (pair)
        {
            uint32_t imm7 =
                (instruction >> 15) &
                0x7F;

            int64_t signedImm =
                static_cast<int64_t>(
                    imm7);

            if (imm7 & 0x40)
                signedImm -= 0x80;

            int64_t byteOffset =
                signedImm * 8;

            uint32_t rt2 =
                (instruction >> 10) &
                0x1F;

            uint32_t rn =
                (instruction >> 5) &
                0x1F;

            uint32_t rt =
                instruction &
                0x1F;

            out
                << (load
                        ? "LDP "
                        : "STP ")
                << arm64RegisterName(
                    rt,
                    true)
                << ", "
                << arm64RegisterName(
                    rt2,
                    true)
                << ", ["
                << arm64RegisterName(
                    rn,
                    true,
                    true);

            if (!postIndex &&
                byteOffset != 0)
            {
                out
                    << ", #"
                    << std::dec
                    << byteOffset;
            }

            out
                << "]";

            if (preIndex)
                out << "!";

            if (postIndex)
            {
                out
                    << ", #"
                    << std::dec
                    << byteOffset;
            }

            return out.str();
        }
    }


    uint32_t registerIndex = 0;
    uint64_t targetPage = 0;

    if (decodeAdrp(
            instruction,
            instructionAddress,
            registerIndex,
            targetPage))
    {
        out
            << "ADRP X"
            << registerIndex
            << ", 0x"
            << std::hex
            << targetPage
            << "  ["
            << describeMachLocation(
                targetPage,
                segments,
                sections
            )
            << "]";

        return out.str();
    }

    // ADR (not ADRP).
    if ((instruction & 0x9F000000U) ==
        0x10000000U)
    {
        uint64_t immlo =
            (instruction >> 29) & 0x3;

        uint64_t immhi =
            (instruction >> 5) & 0x7FFFF;

        uint64_t combined =
            (immhi << 2) | immlo;

        int64_t immediate =
            signExtend21(combined);

        uint64_t target =
            static_cast<uint64_t>(
                static_cast<int64_t>(
                    instructionAddress) +
                immediate
            );

        uint32_t rd =
            instruction & 0x1F;

        out
            << "ADR X"
            << rd
            << ", 0x"
            << std::hex
            << target
            << "  ["
            << describeMachLocation(
                target,
                segments,
                sections
            )
            << "]";

        return out.str();
    }

    uint32_t addDestination = 0;
    uint32_t addSource = 0;
    uint64_t addImmediate = 0;

    if (decodeAddImmediate64(
            instruction,
            addDestination,
            addSource,
            addImmediate))
    {
        out
            << "ADD X"
            << addDestination
            << ", X"
            << addSource
            << ", #0x"
            << std::hex
            << addImmediate;

        return out.str();
    }

    // B / BL immediate.
    uint32_t branchOpcode =
        instruction & 0xFC000000U;

    if (branchOpcode ==
            0x14000000U ||
        branchOpcode ==
            0x94000000U)
    {
        uint64_t imm26 =
            instruction &
            0x03FFFFFFU;

        int64_t displacement =
            signExtend26(imm26) << 2;

        uint64_t target =
            static_cast<uint64_t>(
                static_cast<int64_t>(
                    instructionAddress) +
                displacement
            );

        out
            << (branchOpcode ==
                    0x94000000U
                    ? "BL "
                    : "B ")
            << "0x"
            << std::hex
            << target
            << "  ["
            << describeMachLocation(
                target,
                segments,
                sections
            )
            << "]";

        auto stubIt =
            stubSymbols.find(target);

        if (stubIt !=
            stubSymbols.end())
        {
            out
                << " -> "
                << stubIt->second;
        }

        return out.str();
    }

    // Conditional branch immediate.
    if ((instruction & 0xFF000010U) ==
        0x54000000U)
    {
        uint64_t imm19 =
            (instruction >> 5) &
            0x7FFFFU;

        int64_t displacement =
            signExtend19(imm19) << 2;

        uint64_t target =
            static_cast<uint64_t>(
                static_cast<int64_t>(
                    instructionAddress) +
                displacement
            );

        uint32_t condition =
            instruction & 0xF;

        out
            << "B.cond("
            << std::dec
            << condition
            << ") 0x"
            << std::hex
            << target
            << "  ["
            << describeMachLocation(
                target,
                segments,
                sections
            )
            << "]";

        return out.str();
    }

    // RET
    if ((instruction & 0xFFFFFC1FU) ==
        0xD65F0000U)
    {
        uint32_t rn =
            (instruction >> 5) & 0x1F;

        out
            << "RET X"
            << rn;

        return out.str();
    }

    return "";
}



static uint64_t readLittleEndianU64At(
    const std::vector<char>& bytes,
    size_t offset)
{
    uint64_t value = 0;

    for (size_t i = 0; i < 8; ++i)
    {
        value |=
            static_cast<uint64_t>(
                static_cast<unsigned char>(
                    bytes[offset + i]))
            << (i * 8);
    }

    return value;
}

static bool isFileBackedMachSection(
    const MachSectionInfo& section)
{
    uint32_t sectionType =
        section.flags & SECTION_TYPE;

    if (sectionType == S_ZEROFILL ||
        sectionType == S_GB_ZEROFILL ||
        sectionType == S_THREAD_LOCAL_ZEROFILL)
    {
        return false;
    }

    return section.size != 0;
}

static int runMachOPointerRefs(
    const std::string& executablePath,
    const std::string& targetOffsetText)
{
    uint64_t targetRelativeOffset = 0;

    if (!parseRelativeOffset(
            targetOffsetText,
            targetRelativeOffset))
    {
        std::cerr
            << "Invalid module-relative target offset: "
            << targetOffsetText
            << "\n\n"
            << "Example:\n"
            << "  ./build/AegisLab macho-ptrrefs +0xa58c0\n";

        return 1;
    }

    uint64_t preferredTextBase = 0;
    std::vector<MachSegmentInfo> segments;
    std::vector<MachSectionInfo> sections;

    if (!loadMachSectionLayout(
            executablePath,
            preferredTextBase,
            segments,
            sections))
    {
        std::cerr
            << "Could not read the Roblox Mach-O layout.\n";

        return 1;
    }

    uint64_t targetPreferredAddress =
        preferredTextBase +
        targetRelativeOffset;

    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
    {
        std::cerr
            << "Could not open Mach-O file:\n"
            << "  "
            << executablePath
            << "\n";

        return 1;
    }

    input.seekg(0, std::ios::end);

    std::streamoff fileSize =
        input.tellg();

    if (fileSize <= 0)
    {
        std::cerr
            << "Mach-O file is empty or unreadable.\n";

        return 1;
    }

    input.seekg(0);

    std::vector<char> bytes(
        static_cast<size_t>(
            fileSize)
    );

    input.read(
        bytes.data(),
        fileSize
    );

    if (!input)
    {
        std::cerr
            << "Could not read the Mach-O file.\n";

        return 1;
    }

    struct PointerRef
    {
        uint64_t slotPreferredAddress = 0;
        uint64_t slotRelativeOffset = 0;
        uint64_t fileOffset = 0;
        const MachSectionInfo* section = nullptr;
    };

    std::vector<PointerRef> refs;

    for (const MachSectionInfo& section :
         sections)
    {
        if (!isFileBackedMachSection(section))
            continue;

        // Function-pointer tables and vtable-like data normally live in
        // __DATA*, __AUTH*, or __const sections. Skip executable code and
        // string-heavy sections to keep this search focused.
        bool likelyPointerStorage =
            section.segmentName.rfind(
                "__DATA",
                0) == 0 ||
            section.segmentName.rfind(
                "__AUTH",
                0) == 0 ||
            section.sectionName ==
                "__const" ||
            section.sectionName ==
                "__got" ||
            section.sectionName ==
                "__la_symbol_ptr" ||
            section.sectionName ==
                "__nl_symbol_ptr";

        if (!likelyPointerStorage)
            continue;

        uint64_t sectionFileStart =
            section.fileOffset;

        uint64_t sectionFileEnd =
            section.fileOffset +
            section.size;

        if (sectionFileStart >=
            bytes.size())
        {
            continue;
        }

        sectionFileEnd =
            std::min<uint64_t>(
                sectionFileEnd,
                bytes.size()
            );

        if (sectionFileEnd <
            sectionFileStart + 8)
        {
            continue;
        }

        // Preserve the section's natural VM/file alignment when stepping
        // through qwords.
        uint64_t alignmentAdjustment =
            (8 -
             (sectionFileStart & 7ULL)) &
            7ULL;

        for (uint64_t fileOffset =
                 sectionFileStart +
                 alignmentAdjustment;
             fileOffset + 8 <=
                 sectionFileEnd;
             fileOffset += 8)
        {
            uint64_t value =
                readLittleEndianU64At(
                    bytes,
                    static_cast<size_t>(
                        fileOffset)
                );

            if (value !=
                targetPreferredAddress)
            {
                continue;
            }

            uint64_t slotPreferredAddress =
                section.vmaddr +
                (fileOffset -
                 section.fileOffset);

            PointerRef ref;
            ref.slotPreferredAddress =
                slotPreferredAddress;
            ref.slotRelativeOffset =
                slotPreferredAddress -
                preferredTextBase;
            ref.fileOffset =
                fileOffset;
            ref.section =
                &section;

            refs.push_back(ref);
        }
    }

    std::cout
        << "\nAegisLab Static Pointer References\n"
        << "=================================\n\n"
        << "File: "
        << executablePath
        << "\n"
        << std::hex
        << "Target relative:  +0x"
        << targetRelativeOffset
        << "\n"
        << "Target preferred: 0x"
        << targetPreferredAddress
        << "\n"
        << std::dec
        << "Target location:  "
        << describeMachLocation(
            targetPreferredAddress,
            segments,
            sections
        )
        << "\n\n";

    if (refs.empty())
    {
        std::cout
            << "No exact 64-bit static pointer references found in likely pointer-storage sections.\n\n"
            << "Important: modern Mach-O binaries can encode/rebase pointers through dyld fixups\n"
            << "instead of storing the final preferred virtual address literally in the file.\n"
            << "So a zero result does not prove the function has no indirect references.\n\n";

        return 0;
    }

    auto looksLikePreferredAddress =
        [&](uint64_t value)
        {
            for (const MachSegmentInfo& segment :
                 segments)
            {
                if (value >=
                        segment.vmaddr &&
                    value <
                        segment.vmaddr +
                        segment.vmsize)
                {
                    return true;
                }
            }

            return false;
        };

    for (size_t i = 0;
         i < refs.size();
         ++i)
    {
        const PointerRef& ref =
            refs[i];

        std::cout
            << "Reference "
            << (i + 1)
            << ":\n"
            << std::hex
            << "  Slot preferred: 0x"
            << ref.slotPreferredAddress
            << "\n"
            << "  Slot relative:  +0x"
            << ref.slotRelativeOffset
            << "\n"
            << "  File offset:    0x"
            << ref.fileOffset
            << "\n"
            << std::dec
            << "  Location:       "
            << ref.section->segmentName
            << "::"
            << ref.section->sectionName
            << "\n";

        uint64_t sectionStart =
            ref.section->fileOffset;

        uint64_t sectionEnd =
            std::min<uint64_t>(
                ref.section->fileOffset +
                ref.section->size,
                bytes.size()
            );

        uint64_t contextStart =
            ref.fileOffset >=
                    sectionStart + 24
                ? ref.fileOffset - 24
                : sectionStart;

        uint64_t contextEnd =
            std::min<uint64_t>(
                ref.fileOffset + 32,
                sectionEnd
            );

        // Round context start upward to an 8-byte file boundary.
        contextStart =
            (contextStart + 7ULL) &
            ~7ULL;

        std::cout
            << "  Nearby qwords:\n";

        for (uint64_t fileOffset =
                 contextStart;
             fileOffset + 8 <=
                 contextEnd;
             fileOffset += 8)
        {
            uint64_t value =
                readLittleEndianU64At(
                    bytes,
                    static_cast<size_t>(
                        fileOffset)
                );

            uint64_t slotAddress =
                ref.section->vmaddr +
                (fileOffset -
                 ref.section->fileOffset);

            std::cout
                << (fileOffset ==
                        ref.fileOffset
                        ? "    >> "
                        : "       ")
                << "slot +0x"
                << std::hex
                << (slotAddress -
                    preferredTextBase)
                << " = 0x"
                << value;

            if (looksLikePreferredAddress(
                    value))
            {
                std::cout
                    << "  ["
                    << describeMachLocation(
                        value,
                        segments,
                        sections
                    )
                    << "]";
            }

            std::cout
                << "\n";
        }

        std::cout
            << "\n";
    }

    std::cout
        << std::dec
        << "Exact static pointer references: "
        << refs.size()
        << "\n\n";

    return 0;
}

static int runMachOCallers(
    const std::string& executablePath,
    const std::string& targetOffsetText)
{
    uint64_t targetRelativeOffset = 0;

    if (!parseRelativeOffset(
            targetOffsetText,
            targetRelativeOffset))
    {
        std::cerr
            << "Invalid module-relative target offset: "
            << targetOffsetText
            << "\n\n"
            << "Example:\n"
            << "  ./build/AegisLab macho-callers +0x960fc\n";

        return 1;
    }

    uint64_t preferredTextBase = 0;
    std::vector<MachSegmentInfo> segments;
    std::vector<MachSectionInfo> sections;

    if (!loadMachSectionLayout(
            executablePath,
            preferredTextBase,
            segments,
            sections))
    {
        std::cerr
            << "Could not read the Roblox Mach-O layout.\n";

        return 1;
    }

    const MachSectionInfo* textSection =
        nullptr;

    for (const MachSectionInfo& section :
         sections)
    {
        if (section.segmentName ==
                "__TEXT" &&
            section.sectionName ==
                "__text")
        {
            textSection =
                &section;

            break;
        }
    }

    if (!textSection)
    {
        std::cerr
            << "Could not locate __TEXT::__text.\n";

        return 1;
    }

    uint64_t targetPreferredAddress =
        preferredTextBase +
        targetRelativeOffset;

    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
    {
        std::cerr
            << "Could not open Mach-O file:\n"
            << "  "
            << executablePath
            << "\n";

        return 1;
    }

    input.seekg(0, std::ios::end);

    std::streamoff fileSize =
        input.tellg();

    if (fileSize <= 0)
    {
        std::cerr
            << "Mach-O file is empty or unreadable.\n";

        return 1;
    }

    input.seekg(0);

    std::vector<char> bytes(
        static_cast<size_t>(
            fileSize)
    );

    input.read(
        bytes.data(),
        fileSize
    );

    if (!input)
    {
        std::cerr
            << "Could not read the Mach-O file.\n";

        return 1;
    }

    uint64_t textFileStart =
        textSection->fileOffset;

    uint64_t textFileEnd =
        textSection->fileOffset +
        textSection->size;

    if (textFileEnd >
        bytes.size())
    {
        textFileEnd =
            bytes.size();
    }

    std::vector<uint64_t> callers;

    for (uint64_t fileOffset =
             textFileStart;
         fileOffset + 4 <=
             textFileEnd;
         fileOffset += 4)
    {
        uint32_t instruction =
            readLittleEndianU32(
                bytes,
                static_cast<size_t>(
                    fileOffset)
            );

        if ((instruction &
             0xFC000000U) !=
            0x94000000U)
        {
            continue;
        }

        uint64_t imm26 =
            instruction &
            0x03FFFFFFU;

        int64_t displacement =
            signExtend26(imm26) << 2;

        uint64_t callerPreferredAddress =
            textSection->vmaddr +
            (fileOffset -
             textSection->fileOffset);

        uint64_t branchTarget =
            static_cast<uint64_t>(
                static_cast<int64_t>(
                    callerPreferredAddress) +
                displacement
            );

        if (branchTarget ==
            targetPreferredAddress)
        {
            callers.push_back(
                callerPreferredAddress
            );
        }
    }

    std::cout
        << "\nAegisLab Direct Callers\n"
        << "=======================\n\n"
        << "File: "
        << executablePath
        << "\n"
        << std::hex
        << "Target relative:  +0x"
        << targetRelativeOffset
        << "\n"
        << "Target preferred: 0x"
        << targetPreferredAddress
        << "\n"
        << std::dec
        << "Target location:  "
        << describeMachLocation(
            targetPreferredAddress,
            segments,
            sections
        )
        << "\n\n";

    if (callers.empty())
    {
        std::cout
            << "No direct BL callers found.\n\n"
            << "This only finds direct ARM64 BL instructions whose calculated destination\n"
            << "is exactly the requested address. It does not find BLR/BR indirect calls,\n"
            << "function-pointer calls, tail calls, or references through tables.\n\n";

        return 0;
    }

    for (size_t i = 0;
         i < callers.size();
         ++i)
    {
        uint64_t callerPreferredAddress =
            callers[i];

        uint64_t callerRelativeOffset =
            callerPreferredAddress -
            preferredTextBase;

        std::cout
            << "Caller "
            << (i + 1)
            << ":\n"
            << std::hex
            << "  Preferred addr: 0x"
            << callerPreferredAddress
            << "\n"
            << "  Relative:       +0x"
            << callerRelativeOffset
            << "\n"
            << std::dec
            << "  Location:       "
            << describeMachLocation(
                callerPreferredAddress,
                segments,
                sections
            )
            << "\n"
            << "  Inspect with:   ./build/AegisLab macho-code +0x"
            << std::hex
            << callerRelativeOffset
            << std::dec
            << " 50\n\n";
    }

    std::cout
        << "Direct BL callers: "
        << callers.size()
        << "\n\n";

    return 0;
}

static int runMachOCode(
    const std::string& executablePath,
    const std::string& offsetText,
    int radiusInstructions)
{
    uint64_t relativeOffset = 0;

    if (!parseRelativeOffset(
            offsetText,
            relativeOffset))
    {
        std::cerr
            << "Invalid module-relative offset: "
            << offsetText
            << "\n\n"
            << "Example:\n"
            << "  ./build/AegisLab macho-code +0xa1f598\n";

        return 1;
    }

    if (radiusInstructions < 1)
        radiusInstructions = 1;

    if (radiusInstructions > 64)
        radiusInstructions = 64;

    uint64_t preferredTextBase = 0;
    std::vector<MachSegmentInfo> segments;
    std::vector<MachSectionInfo> sections;

    if (!loadMachSectionLayout(
            executablePath,
            preferredTextBase,
            segments,
            sections))
    {
        std::cerr
            << "Could not read the Roblox Mach-O layout.\n";

        return 1;
    }

    const MachSectionInfo* textSection =
        nullptr;

    for (const MachSectionInfo& section :
         sections)
    {
        if (section.segmentName ==
                "__TEXT" &&
            section.sectionName ==
                "__text")
        {
            textSection =
                &section;

            break;
        }
    }

    if (!textSection)
    {
        std::cerr
            << "Could not locate __TEXT::__text.\n";

        return 1;
    }

    std::unordered_map<uint64_t, std::string>
        stubSymbols;

    bool haveStubSymbols =
        loadMachStubSymbols(
            executablePath,
            sections,
            stubSymbols
        );

    uint64_t preferredAddress =
        preferredTextBase +
        relativeOffset;

    if (preferredAddress <
            textSection->vmaddr ||
        preferredAddress >=
            textSection->vmaddr +
            textSection->size)
    {
        std::cerr
            << "The requested address is not inside __TEXT::__text.\n"
            << "Location: "
            << describeMachLocation(
                preferredAddress,
                segments,
                sections
            )
            << "\n";

        return 1;
    }

    uint64_t instructionAddress =
        preferredAddress &
        ~0x3ULL;

    uint64_t centerFileOffset =
        textSection->fileOffset +
        (instructionAddress -
         textSection->vmaddr);

    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
    {
        std::cerr
            << "Could not open Mach-O file:\n"
            << "  "
            << executablePath
            << "\n";

        return 1;
    }

    input.seekg(0, std::ios::end);

    std::streamoff fileSize =
        input.tellg();

    if (fileSize <= 0)
    {
        std::cerr
            << "Mach-O file is empty or unreadable.\n";

        return 1;
    }

    input.seekg(0);

    std::vector<char> bytes(
        static_cast<size_t>(
            fileSize)
    );

    input.read(
        bytes.data(),
        fileSize
    );

    if (!input)
    {
        std::cerr
            << "Could not read the Mach-O file.\n";

        return 1;
    }

    uint64_t textStart =
        textSection->fileOffset;

    uint64_t textEnd =
        textSection->fileOffset +
        textSection->size;

    uint64_t beforeBytes =
        static_cast<uint64_t>(
            radiusInstructions) * 4;

    uint64_t afterBytes =
        static_cast<uint64_t>(
            radiusInstructions) * 4;

    uint64_t startFileOffset =
        centerFileOffset > beforeBytes
            ? centerFileOffset -
                beforeBytes
            : textStart;

    if (startFileOffset < textStart)
        startFileOffset = textStart;

    startFileOffset &=
        ~0x3ULL;

    uint64_t endFileOffset =
        centerFileOffset +
        afterBytes +
        4;

    if (endFileOffset > textEnd)
        endFileOffset = textEnd;

    std::cout
        << "\nAegisLab ARM64 Code Context\n"
        << "===========================\n\n"
        << "File: "
        << executablePath
        << "\n"
        << std::hex
        << "Requested relative: +0x"
        << relativeOffset
        << "\n"
        << "Preferred address:   0x"
        << preferredAddress
        << "\n"
        << std::dec
        << "Location:            "
        << describeMachLocation(
            preferredAddress,
            segments,
            sections
        )
        << "\n"
        << "Radius:              "
        << radiusInstructions
        << " instructions\n"
        << "Imported stubs:      ";

    if (haveStubSymbols)
    {
        std::cout
            << stubSymbols.size();
    }
    else
    {
        std::cout
            << "unavailable";
    }

    std::cout
        << "\n\n";

    for (uint64_t fileOffset =
             startFileOffset;
         fileOffset + 4 <=
             endFileOffset;
         fileOffset += 4)
    {
        uint32_t instruction =
            readLittleEndianU32(
                bytes,
                static_cast<size_t>(
                    fileOffset)
            );

        uint64_t currentAddress =
            textSection->vmaddr +
            (fileOffset -
             textSection->fileOffset);

        uint64_t currentRelative =
            currentAddress -
            preferredTextBase;

        bool isCenter =
            currentAddress ==
            instructionAddress;

        std::string decoded =
            decodeArm64Summary(
                instruction,
                currentAddress,
                segments,
                sections,
                stubSymbols
            );

        std::cout
            << (isCenter ? ">> " : "   ")
            << std::hex
            << "+0x"
            << currentRelative
            << "  0x"
            << currentAddress
            << "  "
            << std::setw(8)
            << std::setfill('0')
            << instruction
            << std::setfill(' ')
            << std::dec;

        if (!decoded.empty())
        {
            std::cout
                << "  "
                << decoded;
        }

        std::cout
            << "\n";
    }

    std::cout
        << "\n"
        << ">> marks the requested instruction.\n"
        << "Only a small ARM64 subset is decoded; undecoded lines still show the raw 32-bit instruction word.\n\n";

    return 0;
}

static int runLiveInfo(pid_t robloxPid)
{
    char executablePath[PROC_PIDPATHINFO_MAXSIZE] = {};

    if (proc_pidpath(
            robloxPid,
            executablePath,
            sizeof(executablePath)) <= 0)
    {
        std::cerr
            << "Could not read RobloxPlayer executable path.\n";

        return 1;
    }

    uint64_t address = 0;
    int regionCount = 0;

    bool foundExecutableImage = false;
    uint64_t imageStart = 0;
    uint64_t imageEnd = 0;

    std::vector<Region> executableRegions;

    while (true)
    {
        proc_regionwithpathinfo regionInfo{};

        int result =
            proc_pidinfo(
                robloxPid,
                PROC_PIDREGIONPATHINFO,
                address,
                &regionInfo,
                sizeof(regionInfo)
            );

        if (result <= 0)
            break;

        const auto& region =
            regionInfo.prp_prinfo;

        uint64_t start =
            region.pri_address;

        uint64_t size =
            region.pri_size;

        uint64_t end =
            start + size;

        std::string path;

        if (regionInfo.prp_vip.vip_path[0] != '\0')
            path = regionInfo.prp_vip.vip_path;

        ++regionCount;

        if (path == executablePath)
        {
            Region item;

            item.start = start;
            item.end = end;
            item.size = size;
            item.protection =
                protectionString(
                    region.pri_protection
                );
            item.path = path;

            executableRegions.push_back(item);

            if (!foundExecutableImage)
            {
                imageStart = start;
                imageEnd = end;
                foundExecutableImage = true;
            }
            else
            {
                imageStart =
                    std::min(
                        imageStart,
                        start
                    );

                imageEnd =
                    std::max(
                        imageEnd,
                        end
                    );
            }
        }

        if (end <= address)
            break;

        address = end;
    }

    std::cout
        << "\nAegisLab Live Runtime\n"
        << "=====================\n\n"
        << "Target: RobloxPlayer\n"
        << "PID: " << robloxPid << "\n"
        << "Path: " << executablePath << "\n"
        << "Mapped regions: "
        << regionCount
        << "\n\n";

    if (!foundExecutableImage)
    {
        std::cout
            << "Roblox executable mappings were not found.\n";

        return 1;
    }

    std::cout
        << "Roblox executable image mappings\n"
        << "--------------------------------\n";

    for (const Region& region : executableRegions)
    {
        std::cout
            << "0x"
            << std::hex
            << std::setw(16)
            << std::setfill('0')
            << region.start
            << " - 0x"
            << std::setw(16)
            << region.end
            << std::dec
            << "  "
            << region.protection
            << "\n";
    }

    std::cout
        << "\nImage range: 0x"
        << std::hex
        << imageStart
        << " - 0x"
        << imageEnd
        << std::dec
        << "\n"
        << "Image span: "
        << (imageEnd - imageStart)
        << " bytes\n\n";

    std::cout
        << "Runtime object traversal status\n"
        << "-------------------------------\n"
        << "SDK metadata is loaded, but it does not contain "
        << "DataModel/Instance memory offsets.\n"
        << "AegisLab will not guess those offsets. "
        << "Live object traversal needs a verified layout "
        << "for the exact Roblox build.\n\n";

    return 0;
}


// -----------------------------------------------------------------------------
// Build identity + read-only live Humanoid validation
// -----------------------------------------------------------------------------

static constexpr const char* kVerifiedRobloxUuid =
    "1807A3EA-4BD2-3B5C-9F4A-E002284A876A";

static constexpr uint64_t kHumanoidHealthOffset = 0xD0;
static constexpr uint64_t kHumanoidMaxHealthOffset = 0xE8;
static constexpr uint64_t kHumanoidWalkSpeedOffset = 0x110;

static bool loadMachOUuid(
    const std::string& executablePath,
    std::string& uuidText)
{
    std::ifstream input(
        executablePath,
        std::ios::binary
    );

    if (!input)
        return false;

    mach_header_64 header{};

    input.read(
        reinterpret_cast<char*>(&header),
        sizeof(header)
    );

    if (!input ||
        header.magic != MH_MAGIC_64)
    {
        return false;
    }

    std::streamoff commandOffset =
        static_cast<std::streamoff>(
            sizeof(mach_header_64)
        );

    for (uint32_t i = 0;
         i < header.ncmds;
         ++i)
    {
        input.seekg(commandOffset);

        load_command command{};

        input.read(
            reinterpret_cast<char*>(&command),
            sizeof(command)
        );

        if (!input ||
            command.cmdsize < sizeof(load_command))
        {
            return false;
        }

        if (command.cmd == LC_UUID)
        {
            input.seekg(commandOffset);

            uuid_command uuidCommand{};

            input.read(
                reinterpret_cast<char*>(&uuidCommand),
                sizeof(uuidCommand)
            );

            if (!input)
                return false;

            std::ostringstream out;

            out
                << std::uppercase
                << std::hex
                << std::setfill('0');

            for (int byteIndex = 0;
                 byteIndex < 16;
                 ++byteIndex)
            {
                if (byteIndex == 4 ||
                    byteIndex == 6 ||
                    byteIndex == 8 ||
                    byteIndex == 10)
                {
                    out << '-';
                }

                out
                    << std::setw(2)
                    << static_cast<unsigned int>(
                        uuidCommand.uuid[byteIndex]
                    );
            }

            uuidText = out.str();
            return true;
        }

        commandOffset +=
            static_cast<std::streamoff>(
                command.cmdsize
            );
    }

    return false;
}

static int runMachOUuid(
    const std::string& executablePath)
{
    std::string uuidText;

    if (!loadMachOUuid(
            executablePath,
            uuidText))
    {
        std::cerr
            << "Could not read LC_UUID from:\n"
            << "  "
            << executablePath
            << "\n";

        return 1;
    }

    std::cout
        << "\nAegisLab Roblox Build Identity\n"
        << "================================\n\n"
        << "File: "
        << executablePath
        << "\n"
        << "UUID: "
        << uuidText
        << "\n\n"
        << "Verified Humanoid layout for this project:\n"
        << "  Health:     +0x"
        << std::hex
        << kHumanoidHealthOffset
        << "\n"
        << "  MaxHealth:  +0x"
        << kHumanoidMaxHealthOffset
        << "\n"
        << "  WalkSpeed:  +0x"
        << kHumanoidWalkSpeedOffset
        << std::dec
        << "\n\n";

    if (uuidText == kVerifiedRobloxUuid)
    {
        std::cout
            << "Layout status: UUID matches the build used to verify "
            << "these offsets.\n\n";
    }
    else
    {
        std::cout
            << "Layout status: UUID DOES NOT match the build used to "
            << "verify these offsets.\n"
            << "AegisLab will not use the saved Humanoid offsets for "
            << "live scanning on this build.\n\n";
    }

    return 0;
}

static bool readTaskMemory(
    mach_port_t task,
    uint64_t address,
    void* destination,
    size_t size)
{
    mach_vm_size_t bytesRead = 0;

    kern_return_t result =
        mach_vm_read_overwrite(
            task,
            static_cast<mach_vm_address_t>(address),
            static_cast<mach_vm_size_t>(size),
            reinterpret_cast<mach_vm_address_t>(
                destination
            ),
            &bytesRead
        );

    return result == KERN_SUCCESS &&
           bytesRead == size;
}

static bool readTaskFloat(
    mach_port_t task,
    uint64_t address,
    float& value)
{
    return readTaskMemory(
        task,
        address,
        &value,
        sizeof(value)
    );
}

static bool floatNear(
    float value,
    float expected,
    float tolerance = 0.001f)
{
    return std::isfinite(value) &&
           std::fabs(value - expected) <= tolerance;
}

static bool addressIsRobloxImageMapping(
    pid_t robloxPid,
    uint64_t address,
    const std::string& executablePath,
    bool requireExecutable)
{
    proc_regionwithpathinfo regionInfo{};

    int result =
        proc_pidinfo(
            robloxPid,
            PROC_PIDREGIONPATHINFO,
            address,
            &regionInfo,
            sizeof(regionInfo)
        );

    if (result <= 0)
        return false;

    const auto& region =
        regionInfo.prp_prinfo;

    uint64_t start = region.pri_address;
    uint64_t end = start + region.pri_size;

    if (address < start ||
        address >= end)
    {
        return false;
    }

    if (requireExecutable &&
        (region.pri_protection & VM_PROT_EXECUTE) == 0)
    {
        return false;
    }

    std::string path;

    if (regionInfo.prp_vip.vip_path[0] != '\0')
        path = regionInfo.prp_vip.vip_path;

    return path == executablePath;
}

static bool objectHasRobloxVtable(
    mach_port_t task,
    pid_t robloxPid,
    uint64_t objectBase,
    const std::string& executablePath,
    uint64_t& vtable)
{
    if (!readTaskMemory(
            task,
            objectBase,
            &vtable,
            sizeof(vtable)))
    {
        return false;
    }

    // The object's first pointer normally points at a vtable stored in a
    // read-only Roblox image mapping such as __DATA_CONST, not executable code.
    if (!addressIsRobloxImageMapping(
            robloxPid,
            vtable,
            executablePath,
            false))
    {
        return false;
    }

    uint64_t firstVirtualFunction = 0;

    if (!readTaskMemory(
            task,
            vtable,
            &firstVirtualFunction,
            sizeof(firstVirtualFunction)))
    {
        return false;
    }

    return addressIsRobloxImageMapping(
        robloxPid,
        firstVirtualFunction,
        executablePath,
        true
    );
}

static bool verifyHumanoidCandidateBase(
    mach_port_t task,
    pid_t robloxPid,
    uint64_t objectBase,
    const std::string& executablePath,
    float& health,
    float& maxHealth,
    float& walkSpeed,
    uint64_t& vtable)
{
    if ((objectBase & 0x7) != 0)
        return false;

    if (!objectHasRobloxVtable(
            task,
            robloxPid,
            objectBase,
            executablePath,
            vtable))
    {
        return false;
    }

    if (!readTaskFloat(
            task,
            objectBase + kHumanoidHealthOffset,
            health) ||
        !readTaskFloat(
            task,
            objectBase + kHumanoidMaxHealthOffset,
            maxHealth) ||
        !readTaskFloat(
            task,
            objectBase + kHumanoidWalkSpeedOffset,
            walkSpeed))
    {
        return false;
    }

    return std::isfinite(health) &&
           std::isfinite(maxHealth) &&
           std::isfinite(walkSpeed);
}

static int runLiveReadTest(pid_t robloxPid)
{
    char executablePathBuffer[
        PROC_PIDPATHINFO_MAXSIZE
    ] = {};

    if (proc_pidpath(
            robloxPid,
            executablePathBuffer,
            sizeof(executablePathBuffer)) <= 0)
    {
        std::cerr
            << "Could not read RobloxPlayer executable path.\n";

        return 1;
    }

    std::string executablePath =
        executablePathBuffer;

    std::string uuidText;

    if (!loadMachOUuid(
            executablePath,
            uuidText))
    {
        std::cerr
            << "Could not read RobloxPlayer LC_UUID.\n";

        return 1;
    }

    std::cout
        << "\nAegisLab Live Read Test\n"
        << "=======================\n\n"
        << "PID:  "
        << robloxPid
        << "\n"
        << "UUID: "
        << uuidText
        << "\n\n";

    mach_port_t task = MACH_PORT_NULL;

    kern_return_t taskResult =
        task_for_pid(
            mach_task_self(),
            robloxPid,
            &task
        );

    if (taskResult != KERN_SUCCESS)
    {
        std::cerr
            << "Read access was not granted by macOS.\n"
            << "task_for_pid returned: "
            << taskResult
            << " ("
            << mach_error_string(taskResult)
            << ")\n\n"
            << "AegisLab did not attempt to bypass that protection.\n";

        return 2;
    }

    uint64_t liveImageBase = 0;

    if (!findLiveImageBase(
            robloxPid,
            executablePath,
            liveImageBase))
    {
        std::cerr
            << "Could not locate the live Roblox image base.\n";

        mach_port_deallocate(
            mach_task_self(),
            task
        );

        return 1;
    }

    uint32_t liveMagic = 0;

    bool readWorked =
        readTaskMemory(
            task,
            liveImageBase,
            &liveMagic,
            sizeof(liveMagic)
        );

    mach_port_deallocate(
        mach_task_self(),
        task
    );

    if (!readWorked)
    {
        std::cerr
            << "A task port was obtained, but reading the live image "
            << "failed.\n";

        return 3;
    }

    std::cout
        << "Live image base: 0x"
        << std::hex
        << liveImageBase
        << "\n"
        << "Mach-O magic:    0x"
        << liveMagic
        << std::dec
        << "\n\n";

    if (liveMagic != MH_MAGIC_64)
    {
        std::cout
            << "Read succeeded, but the image-base validation did not "
            << "match MH_MAGIC_64.\n\n";

        return 4;
    }

    std::cout
        << "Read-only live memory access is working.\n\n";

    return 0;
}

struct HumanoidLiveCandidate
{
    uint64_t base = 0;
    uint64_t vtable = 0;
    float health = 0.0f;
    float maxHealth = 0.0f;
    float walkSpeed = 0.0f;
    bool maxHealthMatches = false;
    bool walkSpeedMatches = false;
};

static int runLiveHumanoidScan(
    pid_t robloxPid,
    float expectedMaxHealth,
    float expectedWalkSpeed)
{
    char executablePathBuffer[
        PROC_PIDPATHINFO_MAXSIZE
    ] = {};

    if (proc_pidpath(
            robloxPid,
            executablePathBuffer,
            sizeof(executablePathBuffer)) <= 0)
    {
        std::cerr
            << "Could not read RobloxPlayer executable path.\n";

        return 1;
    }

    std::string executablePath =
        executablePathBuffer;

    std::string uuidText;

    if (!loadMachOUuid(
            executablePath,
            uuidText))
    {
        std::cerr
            << "Could not read RobloxPlayer LC_UUID.\n";

        return 1;
    }

    std::cout
        << "\nAegisLab Live Humanoid Scan\n"
        << "===========================\n\n"
        << "PID:                 "
        << robloxPid
        << "\n"
        << "Roblox UUID:         "
        << uuidText
        << "\n"
        << "Expected MaxHealth:  "
        << expectedMaxHealth
        << "\n"
        << "Expected WalkSpeed:  "
        << expectedWalkSpeed
        << "\n\n";

    if (uuidText != kVerifiedRobloxUuid)
    {
        std::cerr
            << "The installed Roblox build does not match the build "
            << "for which the Humanoid offsets were verified.\n"
            << "Expected UUID: "
            << kVerifiedRobloxUuid
            << "\n"
            << "Current UUID:  "
            << uuidText
            << "\n\n"
            << "Scan stopped instead of guessing with stale offsets.\n";

        return 2;
    }

    mach_port_t task = MACH_PORT_NULL;

    kern_return_t taskResult =
        task_for_pid(
            mach_task_self(),
            robloxPid,
            &task
        );

    if (taskResult != KERN_SUCCESS)
    {
        std::cerr
            << "Read access was not granted by macOS.\n"
            << "task_for_pid returned: "
            << taskResult
            << " ("
            << mach_error_string(taskResult)
            << ")\n\n"
            << "AegisLab did not attempt to bypass that protection.\n";

        return 3;
    }

    constexpr uint64_t chunkSize =
        1024ULL * 1024ULL;

    std::set<uint64_t> candidateBases;
    uint64_t regionsScanned = 0;
    uint64_t bytesReadTotal = 0;

    uint64_t address = 0;

    while (true)
    {
        proc_regionwithpathinfo regionInfo{};

        int result =
            proc_pidinfo(
                robloxPid,
                PROC_PIDREGIONPATHINFO,
                address,
                &regionInfo,
                sizeof(regionInfo)
            );

        if (result <= 0)
            break;

        const auto& region =
            regionInfo.prp_prinfo;

        uint64_t start = region.pri_address;
        uint64_t size = region.pri_size;
        uint64_t end = start + size;

        bool readable =
            (region.pri_protection & VM_PROT_READ) != 0;

        bool writable =
            (region.pri_protection & VM_PROT_WRITE) != 0;

        if (readable &&
            writable &&
            size >= kHumanoidWalkSpeedOffset + sizeof(float))
        {
            ++regionsScanned;

            for (uint64_t chunkStart = start;
                 chunkStart < end;)
            {
                uint64_t remaining =
                    end - chunkStart;

                size_t requestSize =
                    static_cast<size_t>(
                        std::min<uint64_t>(
                            chunkSize,
                            remaining
                        )
                    );

                std::vector<unsigned char> buffer(
                    requestSize
                );

                mach_vm_size_t bytesRead = 0;

                kern_return_t readResult =
                    mach_vm_read_overwrite(
                        task,
                        static_cast<mach_vm_address_t>(
                            chunkStart
                        ),
                        static_cast<mach_vm_size_t>(
                            requestSize
                        ),
                        reinterpret_cast<mach_vm_address_t>(
                            buffer.data()
                        ),
                        &bytesRead
                    );

                if (readResult == KERN_SUCCESS &&
                    bytesRead >= sizeof(float))
                {
                    bytesReadTotal += bytesRead;

                    for (size_t offset = 0;
                         offset + sizeof(float) <= bytesRead;
                         offset += sizeof(float))
                    {
                        float value = 0.0f;

                        std::memcpy(
                            &value,
                            buffer.data() + offset,
                            sizeof(value)
                        );

                        bool looksLikeMaxHealth =
                            floatNear(
                                value,
                                expectedMaxHealth
                            );

                        bool looksLikeWalkSpeed =
                            floatNear(
                                value,
                                expectedWalkSpeed
                            );

                        if (!looksLikeMaxHealth &&
                            !looksLikeWalkSpeed)
                        {
                            continue;
                        }

                        uint64_t valueAddress =
                            chunkStart + offset;

                        uint64_t objectBase = 0;

                        if (looksLikeMaxHealth)
                        {
                            if (valueAddress <
                                kHumanoidMaxHealthOffset)
                            {
                                continue;
                            }

                            objectBase =
                                valueAddress -
                                kHumanoidMaxHealthOffset;
                        }
                        else
                        {
                            if (valueAddress <
                                kHumanoidWalkSpeedOffset)
                            {
                                continue;
                            }

                            objectBase =
                                valueAddress -
                                kHumanoidWalkSpeedOffset;
                        }

                        if ((objectBase & 0x7) != 0)
                            continue;

                        uint64_t vtable = 0;

                        if (!objectHasRobloxVtable(
                                task,
                                robloxPid,
                                objectBase,
                                executablePath,
                                vtable))
                        {
                            continue;
                        }

                        candidateBases.insert(
                            objectBase
                        );
                    }
                }

                if (requestSize == 0 ||
                    chunkStart >
                        UINT64_MAX - requestSize)
                {
                    break;
                }

                chunkStart += requestSize;
            }
        }

        if (end <= address)
            break;

        address = end;
    }

    std::vector<HumanoidLiveCandidate> candidates;

    for (uint64_t objectBase : candidateBases)
    {
        HumanoidLiveCandidate candidate;
        candidate.base = objectBase;

        if (!verifyHumanoidCandidateBase(
                task,
                robloxPid,
                objectBase,
                executablePath,
                candidate.health,
                candidate.maxHealth,
                candidate.walkSpeed,
                candidate.vtable))
        {
            continue;
        }

        candidate.maxHealthMatches =
            floatNear(
                candidate.maxHealth,
                expectedMaxHealth
            );

        candidate.walkSpeedMatches =
            floatNear(
                candidate.walkSpeed,
                expectedWalkSpeed
            );

        candidates.push_back(candidate);
    }

    mach_port_deallocate(
        mach_task_self(),
        task
    );

    std::cout
        << "Readable+writable regions scanned: "
        << regionsScanned
        << "\n"
        << "Bytes successfully read:          "
        << bytesReadTotal
        << "\n"
        << "Object-shaped candidates:         "
        << candidates.size()
        << "\n\n";

    size_t strongCount = 0;

    std::cout
        << "Strong matches (MaxHealth + WalkSpeed)\n"
        << "--------------------------------------\n";

    for (const HumanoidLiveCandidate& candidate :
         candidates)
    {
        if (!candidate.maxHealthMatches ||
            !candidate.walkSpeedMatches)
        {
            continue;
        }

        ++strongCount;

        std::cout
            << "Candidate "
            << strongCount
            << "\n"
            << "  Base:      0x"
            << std::hex
            << candidate.base
            << "\n"
            << "  VTable:    0x"
            << candidate.vtable
            << std::dec
            << "\n"
            << "  Health:    "
            << candidate.health
            << "\n"
            << "  MaxHealth: "
            << candidate.maxHealth
            << "\n"
            << "  WalkSpeed: "
            << candidate.walkSpeed
            << "\n\n";
    }

    if (strongCount == 0)
    {
        std::cout
            << "  None.\n\n";
    }

    size_t maxOnlyCount = 0;

    std::cout
        << "MaxHealth matches where WalkSpeed differs\n"
        << "-----------------------------------------\n";

    for (const HumanoidLiveCandidate& candidate :
         candidates)
    {
        if (!candidate.maxHealthMatches ||
            candidate.walkSpeedMatches)
        {
            continue;
        }

        if (maxOnlyCount >= 20)
            break;

        ++maxOnlyCount;

        std::cout
            << "  Base 0x"
            << std::hex
            << candidate.base
            << std::dec
            << " | Health "
            << candidate.health
            << " | MaxHealth "
            << candidate.maxHealth
            << " | WalkSpeed "
            << candidate.walkSpeed
            << "\n";
    }

    if (maxOnlyCount == 0)
        std::cout << "  None.\n";

    size_t walkOnlyCount = 0;

    std::cout
        << "\nWalkSpeed matches where MaxHealth differs\n"
        << "-----------------------------------------\n";

    for (const HumanoidLiveCandidate& candidate :
         candidates)
    {
        if (!candidate.walkSpeedMatches ||
            candidate.maxHealthMatches)
        {
            continue;
        }

        if (walkOnlyCount >= 20)
            break;

        ++walkOnlyCount;

        std::cout
            << "  Base 0x"
            << std::hex
            << candidate.base
            << std::dec
            << " | Health "
            << candidate.health
            << " | MaxHealth "
            << candidate.maxHealth
            << " | WalkSpeed "
            << candidate.walkSpeed
            << "\n";
    }

    if (walkOnlyCount == 0)
        std::cout << "  None.\n";

    std::cout
        << "\nInterpretation:\n"
        << "  A strong match means the live object agrees with both "
        << "verified native Humanoid fields.\n"
        << "  A one-field match is useful evidence that Audaciga may "
        << "map only that stat to Humanoid.\n"
        << "  No match does not prove the displayed stats are absent; "
        << "it points us toward custom game state instead.\n\n";

    return 0;
}

// ============================================================
// Cordial-style Android ELF inspection
// ============================================================
//
// Cordial loads Roblox's Android libroblox.so through a ported
// Android/bionic linker on Linux.  AegisLab does not reproduce Cordial's
// loader on macOS.  These helpers perform static, read-only ELF analysis
// so we can evaluate an Android libroblox.so before moving it into an
// arm64 Linux runtime.

struct ElfProgramHeaderInfo
{
    uint32_t type = 0;
    uint32_t flags = 0;
    uint64_t offset = 0;
    uint64_t vaddr = 0;
    uint64_t fileSize = 0;
    uint64_t memorySize = 0;
    uint64_t align = 0;
};

struct ElfSectionInfo
{
    uint32_t nameOffset = 0;
    uint32_t type = 0;
    uint64_t flags = 0;
    uint64_t address = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t link = 0;
    uint32_t info = 0;
    uint64_t alignment = 0;
    uint64_t entrySize = 0;
};

static uint16_t readLittleEndianU16At(
    const std::vector<char>& bytes,
    size_t offset)
{
    return
        static_cast<uint16_t>(
            static_cast<unsigned char>(bytes[offset]))
        |
        static_cast<uint16_t>(
            static_cast<uint16_t>(
                static_cast<unsigned char>(bytes[offset + 1])) << 8);
}

static bool loadWholeBinaryFile(
    const std::string& path,
    std::vector<char>& bytes)
{
    std::ifstream input(path, std::ios::binary);

    if (!input)
        return false;

    input.seekg(0, std::ios::end);
    std::streamoff size = input.tellg();

    if (size <= 0)
        return false;

    input.seekg(0);

    bytes.resize(static_cast<size_t>(size));
    input.read(bytes.data(), size);

    return static_cast<bool>(input);
}

static bool isElf64LittleEndian(
    const std::vector<char>& bytes)
{
    if (bytes.size() < 64)
        return false;

    return
        static_cast<unsigned char>(bytes[0]) == 0x7f &&
        bytes[1] == 'E' &&
        bytes[2] == 'L' &&
        bytes[3] == 'F' &&
        static_cast<unsigned char>(bytes[4]) == 2 &&
        static_cast<unsigned char>(bytes[5]) == 1;
}

static std::string elfMachineName(uint16_t machine)
{
    switch (machine)
    {
        case 62:
            return "x86_64";

        case 183:
            return "aarch64";

        default:
            return "machine-" + std::to_string(machine);
    }
}

static std::string elfTypeName(uint16_t type)
{
    switch (type)
    {
        case 1:
            return "REL";

        case 2:
            return "EXEC";

        case 3:
            return "DYN (shared object / PIE)";

        case 4:
            return "CORE";

        default:
            return "type-" + std::to_string(type);
    }
}

static std::string elfSegmentProtection(uint32_t flags)
{
    std::string out = "---";

    // ELF PF_X = 1, PF_W = 2, PF_R = 4.
    if (flags & 4)
        out[0] = 'r';

    if (flags & 2)
        out[1] = 'w';

    if (flags & 1)
        out[2] = 'x';

    return out;
}

static bool parseElfProgramHeaders(
    const std::vector<char>& bytes,
    std::vector<ElfProgramHeaderInfo>& headers)
{
    if (!isElf64LittleEndian(bytes))
        return false;

    const uint64_t programOffset =
        readLittleEndianU64At(bytes, 32);

    const uint16_t entrySize =
        readLittleEndianU16At(bytes, 54);

    const uint16_t entryCount =
        readLittleEndianU16At(bytes, 56);

    if (entrySize < 56)
        return false;

    if (programOffset > bytes.size())
        return false;

    for (uint16_t i = 0; i < entryCount; ++i)
    {
        uint64_t offset =
            programOffset +
            static_cast<uint64_t>(i) * entrySize;

        if (offset + 56 > bytes.size())
            return false;

        ElfProgramHeaderInfo header;
        header.type = readLittleEndianU32(bytes, offset + 0);
        header.flags = readLittleEndianU32(bytes, offset + 4);
        header.offset = readLittleEndianU64At(bytes, offset + 8);
        header.vaddr = readLittleEndianU64At(bytes, offset + 16);
        header.fileSize = readLittleEndianU64At(bytes, offset + 32);
        header.memorySize = readLittleEndianU64At(bytes, offset + 40);
        header.align = readLittleEndianU64At(bytes, offset + 48);

        headers.push_back(header);
    }

    return true;
}

static bool parseElfSectionHeaders(
    const std::vector<char>& bytes,
    std::vector<ElfSectionInfo>& sections)
{
    if (!isElf64LittleEndian(bytes))
        return false;

    const uint64_t sectionOffset =
        readLittleEndianU64At(bytes, 40);

    const uint16_t entrySize =
        readLittleEndianU16At(bytes, 58);

    const uint16_t entryCount =
        readLittleEndianU16At(bytes, 60);

    if (sectionOffset == 0 || entryCount == 0)
        return true;

    if (entrySize < 64 || sectionOffset > bytes.size())
        return false;

    for (uint16_t i = 0; i < entryCount; ++i)
    {
        uint64_t offset =
            sectionOffset +
            static_cast<uint64_t>(i) * entrySize;

        if (offset + 64 > bytes.size())
            return false;

        ElfSectionInfo section;
        section.nameOffset = readLittleEndianU32(bytes, offset + 0);
        section.type = readLittleEndianU32(bytes, offset + 4);
        section.flags = readLittleEndianU64At(bytes, offset + 8);
        section.address = readLittleEndianU64At(bytes, offset + 16);
        section.offset = readLittleEndianU64At(bytes, offset + 24);
        section.size = readLittleEndianU64At(bytes, offset + 32);
        section.link = readLittleEndianU32(bytes, offset + 40);
        section.info = readLittleEndianU32(bytes, offset + 44);
        section.alignment = readLittleEndianU64At(bytes, offset + 48);
        section.entrySize = readLittleEndianU64At(bytes, offset + 56);

        sections.push_back(section);
    }

    return true;
}

static bool elfVirtualAddressToFileOffset(
    uint64_t address,
    const std::vector<ElfProgramHeaderInfo>& headers,
    uint64_t& fileOffset)
{
    constexpr uint32_t PT_LOAD = 1;

    for (const ElfProgramHeaderInfo& header : headers)
    {
        if (header.type != PT_LOAD)
            continue;

        if (address < header.vaddr)
            continue;

        uint64_t within = address - header.vaddr;

        if (within >= header.fileSize)
            continue;

        fileOffset = header.offset + within;
        return true;
    }

    return false;
}

static bool elfFileOffsetToVirtualAddress(
    uint64_t fileOffset,
    const std::vector<ElfProgramHeaderInfo>& headers,
    uint64_t& address)
{
    constexpr uint32_t PT_LOAD = 1;

    for (const ElfProgramHeaderInfo& header : headers)
    {
        if (header.type != PT_LOAD)
            continue;

        if (fileOffset < header.offset)
            continue;

        uint64_t within = fileOffset - header.offset;

        if (within >= header.fileSize)
            continue;

        address = header.vaddr + within;
        return true;
    }

    return false;
}

static std::string readElfCString(
    const std::vector<char>& bytes,
    uint64_t offset,
    uint64_t maximumBytes)
{
    if (offset >= bytes.size())
        return "";

    uint64_t end =
        std::min<uint64_t>(
            bytes.size(),
            offset + maximumBytes
        );

    std::string out;

    for (uint64_t i = offset; i < end; ++i)
    {
        if (bytes[static_cast<size_t>(i)] == '\0')
            break;

        out.push_back(bytes[static_cast<size_t>(i)]);
    }

    return out;
}

static std::string bytesToLowerHex(
    const std::vector<char>& bytes,
    uint64_t offset,
    uint64_t count)
{
    if (offset > bytes.size() ||
        count > bytes.size() - offset)
    {
        return "";
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');

    for (uint64_t i = 0; i < count; ++i)
    {
        out
            << std::setw(2)
            << static_cast<unsigned int>(
                static_cast<unsigned char>(
                    bytes[static_cast<size_t>(offset + i)]
                )
            );
    }

    return out.str();
}

static uint64_t alignUp4(uint64_t value)
{
    return (value + 3ULL) & ~3ULL;
}

static std::string findElfGnuBuildId(
    const std::vector<char>& bytes,
    const std::vector<ElfProgramHeaderInfo>& headers)
{
    constexpr uint32_t PT_NOTE = 4;
    constexpr uint32_t NT_GNU_BUILD_ID = 3;

    for (const ElfProgramHeaderInfo& header : headers)
    {
        if (header.type != PT_NOTE || header.fileSize < 12)
            continue;

        uint64_t cursor = header.offset;
        uint64_t end =
            std::min<uint64_t>(
                bytes.size(),
                header.offset + header.fileSize
            );

        while (cursor + 12 <= end)
        {
            uint32_t nameSize =
                readLittleEndianU32(bytes, cursor + 0);

            uint32_t descriptionSize =
                readLittleEndianU32(bytes, cursor + 4);

            uint32_t type =
                readLittleEndianU32(bytes, cursor + 8);

            uint64_t nameOffset = cursor + 12;
            uint64_t descriptionOffset =
                nameOffset + alignUp4(nameSize);

            uint64_t next =
                descriptionOffset +
                alignUp4(descriptionSize);

            if (next > end ||
                nameOffset + nameSize > end ||
                descriptionOffset + descriptionSize > end)
            {
                break;
            }

            std::string name;

            if (nameSize > 0)
            {
                uint64_t visibleNameSize = nameSize;

                if (bytes[static_cast<size_t>(nameOffset + nameSize - 1)] == '\0')
                    --visibleNameSize;

                name.assign(
                    bytes.data() + static_cast<size_t>(nameOffset),
                    bytes.data() + static_cast<size_t>(nameOffset + visibleNameSize)
                );
            }

            if (type == NT_GNU_BUILD_ID && name == "GNU")
            {
                return bytesToLowerHex(
                    bytes,
                    descriptionOffset,
                    descriptionSize
                );
            }

            if (next <= cursor)
                break;

            cursor = next;
        }
    }

    return "";
}

struct ElfDynamicSummary
{
    std::vector<uint64_t> neededOffsets;
    uint64_t stringTableAddress = 0;
    uint64_t stringTableSize = 0;
    uint64_t sonameOffset = 0;

    uint64_t relaSize = 0;
    uint64_t relaEntrySize = 24;
    uint64_t relSize = 0;
    uint64_t relEntrySize = 16;
    uint64_t pltRelSize = 0;
    uint64_t pltRelType = 0;

    bool textRel = false;
};

static bool parseElfDynamicSummary(
    const std::vector<char>& bytes,
    const std::vector<ElfProgramHeaderInfo>& headers,
    ElfDynamicSummary& summary)
{
    constexpr uint32_t PT_DYNAMIC = 2;

    constexpr int64_t DT_NULL = 0;
    constexpr int64_t DT_NEEDED = 1;
    constexpr int64_t DT_PLTRELSZ = 2;
    constexpr int64_t DT_STRTAB = 5;
    constexpr int64_t DT_RELASZ = 8;
    constexpr int64_t DT_RELAENT = 9;
    constexpr int64_t DT_STRSZ = 10;
    constexpr int64_t DT_SONAME = 14;
    constexpr int64_t DT_RELSZ = 18;
    constexpr int64_t DT_RELENT = 19;
    constexpr int64_t DT_PLTREL = 20;
    constexpr int64_t DT_TEXTREL = 22;
    constexpr int64_t DT_FLAGS = 30;

    constexpr uint64_t DF_TEXTREL = 0x4;

    bool foundDynamic = false;

    for (const ElfProgramHeaderInfo& header : headers)
    {
        if (header.type != PT_DYNAMIC)
            continue;

        foundDynamic = true;

        uint64_t cursor = header.offset;
        uint64_t end =
            std::min<uint64_t>(
                bytes.size(),
                header.offset + header.fileSize
            );

        while (cursor + 16 <= end)
        {
            int64_t tag =
                static_cast<int64_t>(
                    readLittleEndianU64At(bytes, cursor)
                );

            uint64_t value =
                readLittleEndianU64At(bytes, cursor + 8);

            if (tag == DT_NULL)
                break;

            switch (tag)
            {
                case DT_NEEDED:
                    summary.neededOffsets.push_back(value);
                    break;

                case DT_PLTRELSZ:
                    summary.pltRelSize = value;
                    break;

                case DT_STRTAB:
                    summary.stringTableAddress = value;
                    break;

                case DT_RELASZ:
                    summary.relaSize = value;
                    break;

                case DT_RELAENT:
                    if (value != 0)
                        summary.relaEntrySize = value;
                    break;

                case DT_STRSZ:
                    summary.stringTableSize = value;
                    break;

                case DT_SONAME:
                    summary.sonameOffset = value;
                    break;

                case DT_RELSZ:
                    summary.relSize = value;
                    break;

                case DT_RELENT:
                    if (value != 0)
                        summary.relEntrySize = value;
                    break;

                case DT_PLTREL:
                    summary.pltRelType = value;
                    break;

                case DT_TEXTREL:
                    summary.textRel = true;
                    break;

                case DT_FLAGS:
                    if (value & DF_TEXTREL)
                        summary.textRel = true;
                    break;

                default:
                    break;
            }

            cursor += 16;
        }
    }

    return foundDynamic;
}

static std::vector<std::string> resolveElfNeededLibraries(
    const std::vector<char>& bytes,
    const std::vector<ElfProgramHeaderInfo>& headers,
    const ElfDynamicSummary& dynamic)
{
    std::vector<std::string> result;

    if (dynamic.stringTableAddress == 0 ||
        dynamic.stringTableSize == 0)
    {
        return result;
    }

    uint64_t stringTableOffset = 0;

    if (!elfVirtualAddressToFileOffset(
            dynamic.stringTableAddress,
            headers,
            stringTableOffset))
    {
        return result;
    }

    for (uint64_t needed : dynamic.neededOffsets)
    {
        if (needed >= dynamic.stringTableSize)
            continue;

        std::string name =
            readElfCString(
                bytes,
                stringTableOffset + needed,
                dynamic.stringTableSize - needed
            );

        if (!name.empty())
            result.push_back(name);
    }

    return result;
}

static std::string resolveElfSoname(
    const std::vector<char>& bytes,
    const std::vector<ElfProgramHeaderInfo>& headers,
    const ElfDynamicSummary& dynamic)
{
    if (dynamic.stringTableAddress == 0 ||
        dynamic.stringTableSize == 0 ||
        dynamic.sonameOffset >= dynamic.stringTableSize)
    {
        return "";
    }

    uint64_t stringTableOffset = 0;

    if (!elfVirtualAddressToFileOffset(
            dynamic.stringTableAddress,
            headers,
            stringTableOffset))
    {
        return "";
    }

    return readElfCString(
        bytes,
        stringTableOffset + dynamic.sonameOffset,
        dynamic.stringTableSize - dynamic.sonameOffset
    );
}

struct ElfDynsymSummary
{
    uint64_t symbolCount = 0;
    uint64_t javaNativeCount = 0;
    bool hasJniOnLoad = false;
};

static ElfDynsymSummary inspectElfDynamicSymbols(
    const std::vector<char>& bytes,
    const std::vector<ElfSectionInfo>& sections)
{
    constexpr uint32_t SHT_DYNSYM = 11;

    ElfDynsymSummary summary;

    for (const ElfSectionInfo& symbolSection : sections)
    {
        if (symbolSection.type != SHT_DYNSYM ||
            symbolSection.entrySize < 24 ||
            symbolSection.entrySize == 0)
        {
            continue;
        }

        if (symbolSection.link >= sections.size())
            continue;

        const ElfSectionInfo& stringSection =
            sections[symbolSection.link];

        if (symbolSection.offset > bytes.size() ||
            symbolSection.size > bytes.size() - symbolSection.offset ||
            stringSection.offset > bytes.size() ||
            stringSection.size > bytes.size() - stringSection.offset)
        {
            continue;
        }

        uint64_t count =
            symbolSection.size /
            symbolSection.entrySize;

        summary.symbolCount += count;

        for (uint64_t i = 0; i < count; ++i)
        {
            uint64_t symbolOffset =
                symbolSection.offset +
                i * symbolSection.entrySize;

            if (symbolOffset + 24 > bytes.size())
                break;

            uint32_t nameOffset =
                readLittleEndianU32(bytes, symbolOffset);

            if (nameOffset >= stringSection.size)
                continue;

            std::string name =
                readElfCString(
                    bytes,
                    stringSection.offset + nameOffset,
                    stringSection.size - nameOffset
                );

            if (name == "JNI_OnLoad")
                summary.hasJniOnLoad = true;

            if (name.rfind("Java_", 0) == 0)
                ++summary.javaNativeCount;
        }
    }

    return summary;
}

struct ElfStringMatch
{
    uint64_t fileOffset = 0;
    bool hasVirtualAddress = false;
    uint64_t virtualAddress = 0;
};

static std::vector<ElfStringMatch> findExactElfCStrings(
    const std::vector<char>& bytes,
    const std::vector<ElfProgramHeaderInfo>& headers,
    const std::string& text,
    size_t maximumMatches = 128)
{
    std::vector<ElfStringMatch> result;

    if (text.empty() ||
        text.size() + 1 > bytes.size())
    {
        return result;
    }

    for (size_t i = 0;
         i + text.size() < bytes.size();
         ++i)
    {
        if (bytes[i + text.size()] != '\0')
            continue;

        if (i != 0 && bytes[i - 1] != '\0')
            continue;

        bool same = true;

        for (size_t j = 0; j < text.size(); ++j)
        {
            if (bytes[i + j] != text[j])
            {
                same = false;
                break;
            }
        }

        if (!same)
            continue;

        ElfStringMatch match;
        match.fileOffset = i;
        match.hasVirtualAddress =
            elfFileOffsetToVirtualAddress(
                i,
                headers,
                match.virtualAddress
            );

        result.push_back(match);

        if (result.size() >= maximumMatches)
            break;

        i += text.size();
    }

    return result;
}

static int runElfFindExact(
    const std::string& path,
    const std::string& text)
{
    std::vector<char> bytes;

    if (!loadWholeBinaryFile(path, bytes))
    {
        std::cerr
            << "Could not open ELF file:\n  "
            << path
            << "\n";

        return 1;
    }

    if (!isElf64LittleEndian(bytes))
    {
        std::cerr
            << "The file is not a 64-bit little-endian ELF object.\n";

        return 1;
    }

    std::vector<ElfProgramHeaderInfo> headers;

    if (!parseElfProgramHeaders(bytes, headers))
    {
        std::cerr
            << "Could not parse ELF program headers.\n";

        return 1;
    }

    auto matches =
        findExactElfCStrings(
            bytes,
            headers,
            text
        );

    std::cout
        << "\nAegisLab Exact ELF C-String Search\n"
        << "=================================\n\n"
        << "File: "
        << path
        << "\n"
        << "Search: "
        << text
        << "\n\n";

    for (size_t i = 0; i < matches.size(); ++i)
    {
        const ElfStringMatch& match = matches[i];

        std::cout
            << "Match "
            << (i + 1)
            << "\n"
            << "  File offset: 0x"
            << std::hex
            << match.fileOffset
            << std::dec
            << "\n";

        if (match.hasVirtualAddress)
        {
            std::cout
                << "  ELF vaddr:   0x"
                << std::hex
                << match.virtualAddress
                << std::dec
                << "\n";
        }
        else
        {
            std::cout
                << "  ELF vaddr:   (not in PT_LOAD)\n";
        }

        std::cout << "\n";
    }

    std::cout
        << "Matches: "
        << matches.size()
        << "\n\n";

    return 0;
}

static int runCordialProbe(const std::string& path)
{
    std::vector<char> bytes;

    if (!loadWholeBinaryFile(path, bytes))
    {
        std::cerr
            << "Could not open Android engine library:\n  "
            << path
            << "\n\n"
            << "Place an extracted Roblox Android libroblox.so at:\n"
            << "  data/libroblox.so\n"
            << "or pass its real path as the second argument.\n";

        return 1;
    }

    if (!isElf64LittleEndian(bytes))
    {
        std::cerr
            << "The supplied file is not a 64-bit little-endian ELF object.\n";

        return 1;
    }

    const uint16_t elfType =
        readLittleEndianU16At(bytes, 16);

    const uint16_t machine =
        readLittleEndianU16At(bytes, 18);

    const uint64_t entryPoint =
        readLittleEndianU64At(bytes, 24);

    std::vector<ElfProgramHeaderInfo> headers;
    std::vector<ElfSectionInfo> sections;

    if (!parseElfProgramHeaders(bytes, headers) ||
        !parseElfSectionHeaders(bytes, sections))
    {
        std::cerr
            << "Could not parse ELF metadata.\n";

        return 1;
    }

    std::string buildId =
        findElfGnuBuildId(bytes, headers);

    ElfDynamicSummary dynamic;
    bool hasDynamic =
        parseElfDynamicSummary(
            bytes,
            headers,
            dynamic
        );

    auto needed =
        resolveElfNeededLibraries(
            bytes,
            headers,
            dynamic
        );

    std::string soname =
        resolveElfSoname(
            bytes,
            headers,
            dynamic
        );

    ElfDynsymSummary dynsym =
        inspectElfDynamicSymbols(
            bytes,
            sections
        );

    uint64_t relaCount = 0;
    uint64_t relCount = 0;
    uint64_t pltCount = 0;

    if (dynamic.relaEntrySize != 0)
        relaCount = dynamic.relaSize / dynamic.relaEntrySize;

    if (dynamic.relEntrySize != 0)
        relCount = dynamic.relSize / dynamic.relEntrySize;

    if (dynamic.pltRelSize != 0)
    {
        // DT_PLTREL contains DT_RELA (7) or DT_REL (17).
        if (dynamic.pltRelType == 7 &&
            dynamic.relaEntrySize != 0)
        {
            pltCount = dynamic.pltRelSize / dynamic.relaEntrySize;
        }
        else if (dynamic.pltRelType == 17 &&
                 dynamic.relEntrySize != 0)
        {
            pltCount = dynamic.pltRelSize / dynamic.relEntrySize;
        }
    }

    std::cout
        << "\nAegisLab Cordial / Android Engine Probe\n"
        << "=======================================\n\n"
        << "File: "
        << path
        << "\n"
        << "Size: "
        << bytes.size()
        << " bytes\n"
        << "ELF class: 64-bit\n"
        << "Byte order: little-endian\n"
        << "Architecture: "
        << elfMachineName(machine)
        << "\n"
        << "ELF type: "
        << elfTypeName(elfType)
        << "\n"
        << "Entry point: 0x"
        << std::hex
        << entryPoint
        << std::dec
        << "\n";

    if (!buildId.empty())
    {
        std::cout
            << "GNU build ID: "
            << buildId
            << "\n";
    }
    else
    {
        std::cout
            << "GNU build ID: <not found>\n";
    }

    if (!soname.empty())
    {
        std::cout
            << "SONAME: "
            << soname
            << "\n";
    }

    std::cout
        << "Dynamic table: "
        << (hasDynamic ? "present" : "not found")
        << "\n"
        << "DT_TEXTREL: "
        << (dynamic.textRel ? "present" : "absent")
        << "\n"
        << "Dynamic symbols: "
        << dynsym.symbolCount
        << "\n"
        << "Java_* exports: "
        << dynsym.javaNativeCount
        << "\n"
        << "JNI_OnLoad: "
        << (dynsym.hasJniOnLoad ? "exported" : "not found in .dynsym")
        << "\n"
        << "Relocations (non-PLT RELA): "
        << relaCount
        << "\n"
        << "Relocations (non-PLT REL):  "
        << relCount
        << "\n"
        << "PLT relocations:             "
        << pltCount
        << "\n\n";

    std::cout
        << "PT_LOAD segments\n"
        << "----------------\n";

    constexpr uint32_t PT_LOAD = 1;
    size_t loadIndex = 0;
    uint64_t maximumAlignment = 0;

    for (const ElfProgramHeaderInfo& header : headers)
    {
        if (header.type != PT_LOAD)
            continue;

        maximumAlignment =
            std::max(maximumAlignment, header.align);

        std::cout
            << "  ["
            << loadIndex++
            << "] "
            << elfSegmentProtection(header.flags)
            << "  file 0x"
            << std::hex
            << header.offset
            << "..0x"
            << (header.offset + header.fileSize)
            << "  vaddr 0x"
            << header.vaddr
            << "..0x"
            << (header.vaddr + header.memorySize)
            << "  align 0x"
            << header.align
            << std::dec
            << "\n";
    }

    std::cout
        << "\nMaximum PT_LOAD alignment: 0x"
        << std::hex
        << maximumAlignment
        << std::dec
        << "\n\n";

    std::cout
        << "DT_NEEDED libraries\n"
        << "-------------------\n";

    if (needed.empty())
    {
        std::cout << "  <none resolved>\n";
    }
    else
    {
        for (const std::string& library : needed)
            std::cout << "  " << library << "\n";
    }

    std::cout
        << "\nRoblox engine anchors\n"
        << "---------------------\n";

    const std::vector<std::string> anchors =
    {
        "DataModel",
        "Workspace",
        "Players",
        "Instance",
        "Humanoid",
        "Health",
        "MaxHealth",
        "WalkSpeed",
        "JNI_OnLoad"
    };

    for (const std::string& anchor : anchors)
    {
        auto matches =
            findExactElfCStrings(
                bytes,
                headers,
                anchor,
                8
            );

        std::cout
            << "  "
            << std::left
            << std::setw(12)
            << anchor
            << std::right
            << " : "
            << matches.size();

        if (!matches.empty())
        {
            std::cout
                << "  first file+0x"
                << std::hex
                << matches.front().fileOffset;

            if (matches.front().hasVirtualAddress)
            {
                std::cout
                    << "  vaddr 0x"
                    << matches.front().virtualAddress;
            }

            std::cout << std::dec;
        }

        std::cout << "\n";
    }

    std::cout
        << "\nCordial-style interpretation\n"
        << "----------------------------\n";

    if (machine == 183)
    {
        std::cout
            << "  Architecture matches Roblox's arm64-v8a Android ABI.\n";
    }
    else if (machine == 62)
    {
        std::cout
            << "  This is the x86_64 Android ABI, not the ARM64 library for an M1-oriented Linux runtime.\n";
    }
    else
    {
        std::cout
            << "  Architecture is not one of Cordial's documented Roblox host ABIs.\n";
    }

    if (elfType == 3)
    {
        std::cout
            << "  The object is ET_DYN, the expected form for libroblox.so.\n";
    }
    else
    {
        std::cout
            << "  The object is not ET_DYN; verify that this really is libroblox.so.\n";
    }

    if (dynsym.hasJniOnLoad)
    {
        std::cout
            << "  JNI_OnLoad is available for a Cordial-style JNI bootstrap.\n";
    }
    else
    {
        std::cout
            << "  JNI_OnLoad was not recovered from section-based dynamic symbols.\n";
    }

    std::cout
        << "  This command does not load or patch Roblox.  Cordial's loader is Linux/bionic-facing;\n"
        << "  AegisLab on macOS uses this probe to identify the exact Android engine build and\n"
        << "  collect the ELF facts we need before an ARM64 Linux runtime test.\n\n";

    return 0;
}


int main(

    int argc,

    char* argv[])

{

    if (argc < 2)

    {

        std::cout

            << "Usage:\n\n"



            << "Single snapshot:\n"

            << "  ./build/AegisLab menu\n\n"



            << "Timed watch:\n"

            << "  ./build/AegisLab watch flying\n\n"



            << "Compare entire 15-file series:\n"

            << "  ./build/AegisLab compare-series flying\n\n"



            << "Browse generated Roblox SDK:\n"

            << "  ./build/AegisLab sdk Humanoid\n\n"

            << "Show SDK inheritance tree:\n"

            << "  ./build/AegisLab sdk-tree Humanoid\n\n"

            << "Search SDK classes and members:\n"

            << "  ./build/AegisLab sdk-find Health\n\n"

            << "Statically inspect an extracted Android libroblox.so (runs on macOS; does not attach to native Roblox):\n"

            << "  ./build/AegisLab cordial-probe\n"

            << "  ./build/AegisLab cordial-probe data/libroblox.so\n\n"

            << "Search an extracted Android ELF for an exact C string:\n"

            << "  ./build/AegisLab elf-find-exact data/libroblox.so Humanoid\n\n"

            << "Inspect the Roblox Mach-O binary:\n"

            << "  ./build/AegisLab macho-info\n\n"

            << "Search raw substrings inside the Roblox Mach-O:\n"

            << "  ./build/AegisLab macho-find DataModel\n\n"

            << "Search exact null-terminated C strings:\n"

            << "  ./build/AegisLab macho-find-exact DataModel\n\n"

            << "Show complete C strings containing a term:\n"

            << "  ./build/AegisLab macho-find-context DataModel\n\n"

            << "Find ARM64 code references to a module-relative address:\n"

            << "  ./build/AegisLab macho-xref +0x60a69f9\n\n"

            << "Show ARM64 code around a module-relative address:\n"

            << "  ./build/AegisLab macho-code +0xa1f598\n"

            << "  ./build/AegisLab macho-code +0xa1f598 16\n\n"

            << "Find direct ARM64 BL callers of a module-relative address:\n"

            << "  ./build/AegisLab macho-callers +0x960fc\n\n"

            << "Find exact static 64-bit pointers to a module-relative address:\n"

            << "  ./build/AegisLab macho-ptrrefs +0xa58c0\n\n"

            << "Find ARM64 code references to a data address:\n"

            << "  ./build/AegisLab macho-data-xrefs +0x662ff00\n\n"

            << "Find ARM64 code references near a data address:\n"

            << "  ./build/AegisLab macho-data-near +0x662ff00 0x100\n\n"

            << "Show the Roblox Mach-O UUID and saved Humanoid layout status:\n"

            << "  ./build/AegisLab macho-uuid\n\n"

            << "Check the native macOS Roblox build and live image mappings:\n"

            << "  ./build/AegisLab mac-roblox-status\n\n"

            << "Test read-only live process access:\n"

            << "  ./build/AegisLab live-read-test\n\n"

            << "Scan for a live Humanoid using expected MaxHealth and WalkSpeed:\n"

            << "  ./build/AegisLab live-humanoid-scan 3300 11\n\n"

            << "Inspect the live Roblox process and image mappings:\n"

            << "  ./build/AegisLab live-info\n\n"

            << "Translate a Roblox module-relative address:\n"

            << "  ./build/AegisLab addr +0x65f0000\n\n"

            << "Translate a live Roblox address back to a module offset:\n"

            << "  ./build/AegisLab addr-live 0x10b07c000\n";



        return 0;

    }



    std::string command =

        argv[1];



    if (command == "compare-series")

    {

        if (argc < 3)

        {

            std::cerr

                << "Missing series name.\n\n"

                << "Example:\n"

                << "  ./build/AegisLab compare-series flying\n";



            return 1;

        }



        std::string label =

            sanitizeLabel(argv[2]);



        return compareSeries(label);

    }



    if (command == "sdk")

    {

        if (argc < 3)

        {

            std::cerr

                << "Missing Roblox class name.\n\n"

                << "Example:\n"

                << "  ./build/AegisLab sdk Humanoid\n";



            return 1;

        }



        return runSdkCommand(argv[2]);

    }



    if (command == "sdk-tree")
    {
        if (argc < 3)
        {
            std::cerr
                << "Missing Roblox class name.\n\n"
                << "Example:\n"
                << "  ./build/AegisLab sdk-tree Humanoid\n";

            return 1;
        }

        return runSdkTreeCommand(argv[2]);
    }

    if (command == "sdk-find")
    {
        if (argc < 3)
        {
            std::cerr
                << "Missing SDK search text.\n\n"
                << "Example:\n"
                << "  ./build/AegisLab sdk-find Health\n";

            return 1;
        }

        return runSdkFindCommand(argv[2]);
    }

    if (command == "cordial-probe")
    {
        std::string path =
            argc >= 3
                ? argv[2]
                : "data/libroblox.so";

        return runCordialProbe(path);
    }

    if (command == "elf-find-exact")
    {
        if (argc < 4)
        {
            std::cerr
                << "Usage:\n"
                << "  ./build/AegisLab elf-find-exact data/libroblox.so Humanoid\n";

            return 1;
        }

        return runElfFindExact(
            argv[2],
            argv[3]
        );
    }

    if (command == "macho-info")
    {
        std::string executablePath =
            "/Applications/Roblox.app/Contents/MacOS/RobloxPlayer";

        if (argc >= 3)
            executablePath = argv[2];

        return runMachOInfo(
            executablePath
        );
    }

    if (command == "macho-find")
    {
        if (argc < 3)
        {
            std::cerr
                << "Missing search text.\n\n"
                << "Example:\n"
                << "  ./build/AegisLab macho-find DataModel\n";

            return 1;
        }

        return runMachOFind(
            "/Applications/Roblox.app/Contents/MacOS/RobloxPlayer",
            argv[2]
        );
    }

    if (command == "macho-find-exact")
    {
        if (argc < 3)
        {
            std::cerr
                << "Missing search text.\n\n"
                << "Example:\n"
                << "  ./build/AegisLab macho-find-exact DataModel\n";

            return 1;
        }

        return runMachOFindExact(
            "/Applications/Roblox.app/Contents/MacOS/RobloxPlayer",
            argv[2]
        );
    }

    if (command == "macho-find-context")
    {
        if (argc < 3)
        {
            std::cerr
                << "Missing search text.\n\n"
                << "Example:\n"
                << "  ./build/AegisLab macho-find-context DataModel\n";

            return 1;
        }

        return runMachOFindContext(
            "/Applications/Roblox.app/Contents/MacOS/RobloxPlayer",
            argv[2]
        );
    }

    if (command == "macho-xref")
    {
        if (argc < 3)
        {
            std::cerr
                << "Missing module-relative offset.\n\n"
                << "Example:\n"
                << "  ./build/AegisLab macho-xref +0x60a69f9\n";

            return 1;
        }

        return runMachOXref(
            "/Applications/Roblox.app/Contents/MacOS/RobloxPlayer",
            argv[2]
        );
    }

    if (command == "macho-data-near")
    {
        if (argc < 3)
        {
            std::cerr
                << "Missing module-relative target offset.\n\n"
                << "Example:\n"
                << "  ./build/AegisLab macho-data-near +0x662ff00 0x100\n";

            return 1;
        }

        std::string radiusText =
            argc >= 4
                ? argv[3]
                : "0x100";

        return runMachODataNear(
            "/Applications/Roblox.app/Contents/MacOS/RobloxPlayer",
            argv[2],
            radiusText
        );
    }

    if (command == "macho-data-xrefs")
    {
        if (argc < 3)
        {
            std::cerr
                << "Missing module-relative target offset.\n\n"
                << "Example:\n"
                << "  ./build/AegisLab macho-data-xrefs +0x662ff00\n";

            return 1;
        }

        return runMachODataXrefs(
            "/Applications/Roblox.app/Contents/MacOS/RobloxPlayer",
            argv[2]
        );
    }

    if (command == "macho-ptrrefs")
    {
        if (argc < 3)
        {
            std::cerr
                << "Missing module-relative target offset.\n\n"
                << "Example:\n"
                << "  ./build/AegisLab macho-ptrrefs +0xa58c0\n";

            return 1;
        }

        return runMachOPointerRefs(
            "/Applications/Roblox.app/Contents/MacOS/RobloxPlayer",
            argv[2]
        );
    }

    if (command == "macho-callers")
    {
        if (argc < 3)
        {
            std::cerr
                << "Missing module-relative target offset.\n\n"
                << "Example:\n"
                << "  ./build/AegisLab macho-callers +0x960fc\n";

            return 1;
        }

        return runMachOCallers(
            "/Applications/Roblox.app/Contents/MacOS/RobloxPlayer",
            argv[2]
        );
    }

    if (command == "macho-code")
    {
        if (argc < 3)
        {
            std::cerr
                << "Missing module-relative code offset.\n\n"
                << "Example:\n"
                << "  ./build/AegisLab macho-code +0xa1f598\n";

            return 1;
        }

        int radius = 12;

        if (argc >= 4)
        {
            try
            {
                radius =
                    std::stoi(argv[3]);
            }
            catch (...)
            {
                std::cerr
                    << "Invalid instruction radius: "
                    << argv[3]
                    << "\n";

                return 1;
            }
        }

        return runMachOCode(
            "/Applications/Roblox.app/Contents/MacOS/RobloxPlayer",
            argv[2],
            radius
        );
    }

    if (command == "macho-uuid")
    {
        return runMachOUuid(
            "/Applications/Roblox.app/Contents/MacOS/RobloxPlayer"
        );
    }

    pid_t robloxPid =

        findRobloxPlayer();



    if (robloxPid == -1)

    {

        std::cerr

            << "RobloxPlayer was not found.\n";



        return 1;

    }



    if (command == "live-info")

    {

        return runLiveInfo(robloxPid);

    }


    if (command == "mac-roblox-status")
    {
        const std::string nativeRobloxPath =
            "/Applications/Roblox.app/Contents/MacOS/RobloxPlayer";

        std::cout
            << "\nAegisLab Native macOS Roblox Status\n"
            << "====================================\n"
            << "This path uses the installed macOS Mach-O Roblox client.\n"
            << "The Cordial/ELF commands are separate static tools for Android libroblox.so;\n"
            << "they are not used to attach to or replace this native Roblox process.\n\n";

        int uuidResult = runMachOUuid(nativeRobloxPath);

        if (uuidResult != 0)
            return uuidResult;

        return runLiveInfo(robloxPid);
    }


    if (command == "live-read-test")
    {
        return runLiveReadTest(robloxPid);
    }

    if (command == "live-humanoid-scan")
    {
        if (argc < 4)
        {
            std::cerr
                << "Missing expected MaxHealth and WalkSpeed.\n\n"
                << "Example for the Petracorpious values we discussed:\n"
                << "  ./build/AegisLab live-humanoid-scan 3300 11\n";

            return 1;
        }

        float expectedMaxHealth = 0.0f;
        float expectedWalkSpeed = 0.0f;

        try
        {
            expectedMaxHealth = std::stof(argv[2]);
            expectedWalkSpeed = std::stof(argv[3]);
        }
        catch (...)
        {
            std::cerr
                << "MaxHealth and WalkSpeed must be numbers.\n";

            return 1;
        }

        if (!std::isfinite(expectedMaxHealth) ||
            !std::isfinite(expectedWalkSpeed) ||
            expectedMaxHealth <= 0.0f ||
            expectedWalkSpeed <= 0.0f)
        {
            std::cerr
                << "MaxHealth and WalkSpeed must be positive finite numbers.\n";

            return 1;
        }

        return runLiveHumanoidScan(
            robloxPid,
            expectedMaxHealth,
            expectedWalkSpeed
        );
    }



    if (command == "addr")

    {

        if (argc < 3)

        {

            std::cerr

                << "Missing module-relative offset.\n\n"

                << "Example:\n"

                << "  ./build/AegisLab addr +0x65f0000\n";



            return 1;

        }



        return runAddressTranslator(

            robloxPid,

            argv[2]);

    }



    if (command == "addr-live")

    {

        if (argc < 3)

        {

            std::cerr

                << "Missing live address.\n\n"

                << "Example:\n"

                << "  ./build/AegisLab addr-live 0x10b07c000\n";



            return 1;

        }



        return runLiveAddressTranslator(

            robloxPid,

            argv[2]);

    }



    if (command == "watch")

    {

        if (argc < 3)

        {

            std::cerr

                << "Missing watch label.\n";



            return 1;

        }



        std::string label =

            sanitizeLabel(argv[2]);



        constexpr int countdownSeconds = 3;

        constexpr int captureCount = 15;

        constexpr int intervalMilliseconds = 1000;



        std::cout

            << "AegisLab Watch Mode\n"

            << "===================\n"

            << "Target: RobloxPlayer\n"

            << "PID: "

            << robloxPid

            << "\n"

            << "Label: "

            << label

            << "\n\n";



        for (

            int i = countdownSeconds;

            i > 0;

            --i)

        {

            std::cout

                << "Starting in "

                << i

                << "...\n";



            std::this_thread::sleep_for(

                std::chrono::seconds(1)

            );

        }



        std::cout

            << "\nCapturing "

            << captureCount

            << " snapshots at 1 second intervals.\n\n";



        for (

            int i = 1;

            i <= captureCount;

            ++i)

        {

            captureSnapshot(

                robloxPid,

                label,

                i

            );



            if (i < captureCount)

            {

                std::this_thread::sleep_for(

                    std::chrono::milliseconds(

                        intervalMilliseconds

                    )

                );

            }

        }



        std::cout

            << "\nWatch complete.\n";



        return 0;

    }



    std::string label =

        sanitizeLabel(command);



    if (!captureSnapshot(

            robloxPid,

            label,

            1))

    {

        std::cerr

            << "Failed to capture snapshot.\n";



        return 1;

    }



    return 0;

}