#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

class Data {
	alignas(64) mutable std::mutex m0;
	int f0 = 0;
	alignas(64) mutable std::mutex m1;
	int f1 = 0;
public:
	int read0() const { std::lock_guard<std::mutex> l(m0); return f0; }
	void write0(int v) { std::lock_guard<std::mutex> l(m0); f0 = v; }
	int read1() const { std::lock_guard<std::mutex> l(m1); return f1; }
	void write1(int v) { std::lock_guard<std::mutex> l(m1); f1 = v; }
	operator std::string() const {
		std::lock_guard<std::mutex> l0(m0);
		std::lock_guard<std::mutex> l1(m1);
		return std::to_string(f0) + " " + std::to_string(f1);
	}
};

enum Op : unsigned char {R0, W0, R1, W1, STR};
using Weights = std::vector<double>;

void generate_file(const std::string& name, size_t n, const Weights& w, unsigned seed) {
	std::mt19937 gen(seed);
	std::discrete_distribution<int> d(w.begin(), w.end());
	std::ofstream out(name);
	for (size_t i = 0; i < n; i++) {
		switch (d(gen)) {
		case R0: out << "read 0\n"; break;
		case W0: out << "write 0 1\n"; break;
		case R1: out << "read 1\n"; break;
		case W1: out << "write 1 1\n"; break;
		default: out << "string\n";
		}
	}
}
std::vector<Op> load_file(const std::string& name) {
	std::ifstream in(name);
	std::vector<Op> ops;
	std::string cmd; int f, v;
	while (in >> cmd) {
		if (cmd == "string") ops.push_back(STR);
		else if (cmd == "read") { in >> f; ops.push_back(f == 0 ? R0 : R1); }
		else if (cmd == "write") { in >> f >> v; ops.push_back(f == 0 ? W0 : W1); }
	}
	return ops;
}

double run_once(const std::vector<std::vector<Op>>& per, size_t& sink_out) {
	Data data;
	std::vector<size_t> sink(per.size(), 0);
	auto t0 = std::chrono::steady_clock::now();
	{
		std::vector<std::jthread> th;
		for (size_t t = 0; t < per.size(); ++t) {
			th.emplace_back([&, t] {
				size_t s = 0;
				for (Op op : per[t]) {
					switch (op) {
					case R0: s += data.read0(); break;
					case W0: data.write0(1); break;
					case R1: s += data.read1(); break;
					case W1: data.write1(1); break;
					case STR: s += std::string(data).size(); break;
					}
				}
				sink[t] = s;
				});
		}
	}
	auto t1 = std::chrono::steady_clock::now();
	sink_out = 0; 
	for (auto s : sink) sink_out += s;
	return std::chrono::duration<double>(t1 - t0).count();
}

int main() {
	const size_t total = 1'200'000;
	const int runs = 3;
	struct Scenario {
		std::string name; Weights w;
	};
	std::vector<Scenario> sc = {
		{"a", {10, 10, 50, 10, 20}},
		{"b", {20, 20, 20, 20, 20}},
		{"c", {5, 60, 1, 4, 30}}
	};
	double table[3][3];
	std::cout << std::fixed << std::setprecision(4);
	for (int s = 0; s < 3; ++s) {
		for (int T = 1; T <= 3; ++T) {
			std::vector<std::vector<Op>> per;
			for (int t = 0; t < T; ++t) {
				std::string fn = sc[s].name + "_T" + std::to_string(T) + "_" + std::to_string(t) + ".txt";
				generate_file(fn, total / T, sc[s].w, 1000u * s + 10u * T + t);
				per.push_back(load_file(fn));
			}
			double sum = 0;
			for (int r = 0; r < runs; ++r) {
				size_t sink;
				double e = run_once(per, sink);
				sum += e;
				std::cout << "file " << sc[s].name << ", threads " << T
					<< ", start " << r + 1 << ": " << e << " sec\n";
			}
			table[s][T - 1] = sum / runs;
		}
	}
	std::cout << "\n Average time (sec) 1 thread    2 threads    3 threads\n";
	for (int s = 0; s < 3; ++s) {
		std::cout << "file " << std::left << std::setw(12) << sc[s].name << std::right
			<< std::setw(9) << table[s][0] << " "
			<< std::setw(9) << table[s][1] << " "
			<< std::setw(9) << table[s][2] << "\n";
	}
}