#include <iostream>
#include <fstream>
#include <vector>
#include <shared_mutex>
#include <sstream>
#include <atomic>
#include <random>
#include <thread>
#include <chrono>

class LabThreadSafeData {
private:
    std::vector<int> fields;
    std::vector<std::shared_mutex> field_mutexes;
    std::vector<std::atomic<size_t>> read_count;
    std::vector<std::atomic<size_t>> write_count;
    std::atomic<size_t> string_count{ 0 };
    std::atomic<size_t> total_ops{ 0 };

public:
    LabThreadSafeData(size_t m)
        : fields(m, 0), field_mutexes(m),
        read_count(m), write_count(m) {
    }

    int readField(size_t index) {
        std::shared_lock<std::shared_mutex> lock(field_mutexes[index]);
        ++read_count[index];
        ++total_ops;
        return fields[index];
    }

    void writeField(size_t index, int value) {
        std::unique_lock<std::shared_mutex> lock(field_mutexes[index]);
        ++write_count[index];
        ++total_ops;
        fields[index] = value;
    }

    operator std::string() {
        std::ostringstream oss;
        for (size_t i = 0; i < fields.size(); ++i) {
            std::shared_lock<std::shared_mutex> lock(field_mutexes[i]);
            oss << fields[i];
            if (i != fields.size() - 1) oss << ", ";
        }
        ++string_count;
        ++total_ops;
        return oss.str();
    }

    void printStats() {
        size_t ops = total_ops.load();
        if (ops == 0) ops = 1;
        std::cout << "Field stats (Read%, Write%):\n";
        for (size_t i = 0; i < fields.size(); ++i) {
            double r = 100.0 * read_count[i].load() / ops;
            double w = 100.0 * write_count[i].load() / ops;
            std::cout << "Field " << i << ": " << r << "% / " << w << "%\n";
        }
        double s = 100.0 * string_count.load() / ops;
        std::cout << "String requests: " << s << "%\n";
    }
};

struct OpsPercent {
    double read1;
    double write1;
    double read2;
    double write2;
    double read3;
    double write3;
    double string_op;
};

void generateFile(const std::string& filename, int num_ops, const OpsPercent& percents) {

    std::vector<std::string> ops;

    int num_read1 = static_cast<int>(num_ops * percents.read1 / 100.0);
    int num_write1 = static_cast<int>(num_ops * percents.write1 / 100.0);
    int num_read2 = static_cast<int>(num_ops * percents.read2 / 100.0);
    int num_write2 = static_cast<int>(num_ops * percents.write2 / 100.0);
    int num_read3 = static_cast<int>(num_ops * percents.read3 / 100.0);
    int num_write3 = static_cast<int>(num_ops * percents.write3 / 100.0);
    int num_string = static_cast<int>(num_ops * percents.string_op / 100.0);

    for (int i = 0; i < num_read1; ++i) ops.push_back("read 0");
    for (int i = 0; i < num_write1; ++i) ops.push_back("write 0 1");
    for (int i = 0; i < num_read2; ++i) ops.push_back("read 1");
    for (int i = 0; i < num_write2; ++i) ops.push_back("write 1 1");
    for (int i = 0; i < num_read3; ++i) ops.push_back("read 2");
    for (int i = 0; i < num_write3; ++i) ops.push_back("write 2 1");
    for (int i = 0; i < num_string; ++i) ops.push_back("string");

    while ((int)ops.size() < num_ops) ops.push_back("string");

    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(ops.begin(), ops.end(), gen);

    std::ofstream fout(filename);
    for (const auto& op : ops) fout << op << "\n";
}

void executeFile(LabThreadSafeData& data, const std::string& filename) {

    std::ifstream fin(filename);
    std::string line;

    while (std::getline(fin, line)) {

        std::istringstream iss(line);
        std::string cmd;

        iss >> cmd;
        if (cmd == "write") {
            int idx, val;
            iss >> idx >> val;
            data.writeField(idx, val);
        }
        else if (cmd == "read") {
            int idx;
            iss >> idx;
            (void)data.readField(idx);
        }
        else if (cmd == "string") {
            (void)std::string(data);
        }
    }
}

void runThread(LabThreadSafeData& data, const std::string& filename) {
    executeFile(data, filename);
}

int main() {
    const int num_fields = 3;
    const int num_ops = 100000;

    OpsPercent percents_a = {
        10, 10,
        50, 10,
        5,  5,
        10
    };

    OpsPercent percents_b = {
        14.29, 14.29,
        14.29, 14.29,
        14.29,  14.29,
        14.29
    };

    OpsPercent percents_c = {
        40, 5,
        30, 5,
        10,  5,
        5
    };

    generateFile("file_a.txt", num_ops, percents_a);

    generateFile("file_b.txt", num_ops, percents_b);

    generateFile("file_c.txt", num_ops, percents_c);

    std::cout << "Single thread execution\n";
    for (auto fname : { "file_a.txt", "file_b.txt", "file_c.txt" }) {
        LabThreadSafeData data(num_fields);
        auto start = std::chrono::high_resolution_clock::now();
        executeFile(data, fname);
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> dur = end - start;
        std::cout << fname << " time: " << dur.count() << " s\n";
        data.printStats();
        std::cout << "-----------------------\n";
    }

    std::cout << "Two threads execution\n";
    for (auto fname : { "file_a.txt", "file_b.txt", "file_c.txt" }) {
        LabThreadSafeData data(num_fields);
        auto start = std::chrono::high_resolution_clock::now();
        std::thread t1(runThread, std::ref(data), fname);
        std::thread t2(runThread, std::ref(data), fname);
        t1.join();
        t2.join();
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> dur = end - start;
        std::cout << fname << " time: " << dur.count() << " s\n";
        data.printStats();
        std::cout << "-----------------------\n";
    }

    std::cout << "Three threads execution\n";
    for (auto fname : { "file_a.txt", "file_b.txt", "file_c.txt" }) {
        LabThreadSafeData data(num_fields);
        auto start = std::chrono::high_resolution_clock::now();
        std::thread t1(runThread, std::ref(data), fname);
        std::thread t2(runThread, std::ref(data), fname);
        std::thread t3(runThread, std::ref(data), fname);
        t1.join();
        t2.join();
        t3.join();
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> dur = end - start;
        std::cout << fname << " time: " << dur.count() << " s\n";
        data.printStats();
        std::cout << "-----------------------\n";
    }

    return 0;
}