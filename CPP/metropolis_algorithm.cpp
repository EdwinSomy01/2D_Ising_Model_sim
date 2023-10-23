#include <iostream>
#include <iomanip>
#include <random>
#include <chrono>
#include <vector>
#include <string>
#include <fstream>
#include <cctype>
#include <omp.h>

//For convenience
using LatticeType = int;
using Grid = std::vector<std::vector<LatticeType>>;
using GridList = std::vector<Grid>;

//Mersenne Twister engine seeded using system time
std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
//Uniform real distribution
std::uniform_real_distribution<float> urd(1);

//Monte - Carlo timestep
void mc_timestep(LatticeType lattice[][40],float vexp[][9],int T_i,int N){
    //Execute one step for cell
    for(int i = 0;i < N;++i){
        for(int j = 0;j < N;++j){
            LatticeType u = lattice[i][j];
            LatticeType s = lattice[(i-1+N)%N][j] + lattice[(i+1)%N][j] + lattice[i][(i+1)%N] + lattice[i][(j-1+N)%N];
            float accept = urd(rng) - vexp[T_i][u * s + 4];
            lattice[i][j] = std::copysign(u,accept);
        }
    }
}

int main(){
    //This is a C++ program that implements a naive implementation of the Metropolis algorithm.
    //I'm making this to test how much faster C++ is than Python.
    //Conclusion: Much faster. I've also used OpenMP to parallelize this.
    //Compiled with flags -fopenmp and -O2.

    //Input data (part 1)
    int N, t_equilibrium, snapshot_interval, snapshot_count;
    std::cout << "Input size of lattice (N): ";
    std::cin >> N;
    std::cout << "Input equilibriation time: ";
    std::cin >> t_equilibrium;
    std::cout << "Input autocorrelation time: ";
    std::cin >> snapshot_interval;
    std::cout << "Input number of snapshots to take: ";
    std::cin >> snapshot_count;

    //Input data (part 2)
    float T_min, T_max;
    int T_count;
    std::cout << "Input minimum value of temperature: ";
    std::cin >> T_min;
    std::cout << "Input maximum value of temperature: ";
    std::cin >> T_max;
    std::cout << "Input number of values to take (including endpoints): ";
    std::cin >> T_count;

    //Input data (part 3)
    char create_file;
    std::cout << "Create data file? (y/n): ";
    std::cin >> create_file;

    //Set up progress printing
    int pwidth = std::to_string(T_count).length();
    int progress = 0;
    std::cout << "\nProgress: " << std::setw(pwidth) << progress << '/' << T_count;

    //Create lattices
    LatticeType lattices[T_count][40][40];
    for(int k = 0;k < T_count;++k){
        for(int i = 0;i < N;++i){
            for(int j = 0;j < N;++j){
                lattices[k][i][j] = 1;
            }
        }
    }

    //Calculate quotient and remainder of N by 8, needed later while writing file
    int qN8 = N / 8;
    int rN8 = N % 8;

    //Precalculate exponentials...because exp is VERY expensive!
    //This achieved a speedup from ~20s to ~5s (!!) for N = 16
    //and t_eq = 10^5 and T = [0.1,4] in steps of 0.1
    float vexp[T_count][9];
    for(int T_i = 0;T_i < T_count;++T_i){
        for(int j = -4;j <= 4;++j){
            float cur_T = T_min + T_i * (T_max - T_min) / (T_count - 1);
            vexp[T_i][j + 4] = std::exp(j / cur_T);
        }
    }

    std::fstream fout;
    if(create_file == 'y'){
        //Create output file
        std::string filename = "data_" + std::to_string(N) + ".dat";
        fout.open(filename, std::ios_base::out | std::ios_base::binary);
        if(!fout.is_open()){
            std::cout << "Failed to create data file!" << std::endl;
            return 0;
        }
        //Write list of parameters
        fout.write(reinterpret_cast<const char*>(&N),sizeof(int));
        fout.write(reinterpret_cast<const char*>(&t_equilibrium),sizeof(int));
        fout.write(reinterpret_cast<const char*>(&snapshot_interval),sizeof(int));
        fout.write(reinterpret_cast<const char*>(&snapshot_count),sizeof(int));
        fout.write(reinterpret_cast<const char*>(&T_min),sizeof(float));
        fout.write(reinterpret_cast<const char*>(&T_max),sizeof(float));
        fout.write(reinterpret_cast<const char*>(&T_count),sizeof(int));
    }

    //Start timing
    double start = omp_get_wtime();

    //For each value of temperature...
    #pragma omp parallel for
    for(int T_i = 0;T_i < T_count;++T_i){
        //Calculate current temperature
        float cur_T = T_min + T_i * (T_max - T_min) / (T_count - 1);
        
        //Allow lattice to equilibriate
        for(int t_i = 0;t_i < t_equilibrium;++t_i){
            mc_timestep(lattices[T_i], vexp, T_i, N);
        }

        //Start taking snapshots
        for(int snap = 0;snap < snapshot_count;snap++){
            for(int t_i = 0;t_i < snapshot_interval;++t_i){
                mc_timestep(lattices[T_i], vexp, T_i, N);
            }
            if(create_file == 'y'){
                //Write snapshot to file
                #pragma omp critical
                {
                    //Write temperature index
                    fout.write(reinterpret_cast<const char*>(&T_i),sizeof(int));
                    //Write lattice by mapping bools to blocks of 8 (1 byte = 8 bits)
                    for(int i = 0;i < N;++i){
                        for(int j = 0;j < qN8;j += 8){
                            int num = (1 << 8) - 1;
                            for(int k = 0;k < 8;++k){
                                num += lattices[T_i][i][8 * j + k] << k;
                            }
                            char sto = num / 2;
                            fout.write(reinterpret_cast<const char*>(&sto),sizeof(sto));
                        }
                        //Possible remainder block
                        if(rN8 > 0){
                            int num = (1 << rN8) - 1;
                            for(int k = 0;k < rN8;++k){
                                num += lattices[T_i][i][8 * qN8 + k] << k;
                            }
                            char sto = num / 2;
                            fout.write(reinterpret_cast<const char*>(&sto),sizeof(sto));
                        }
                    }
                }
            }
        }

        #pragma omp critical
        {
            ++progress;
            std::cout << "\rProgress: " << std::setw(pwidth) << progress << '/' << T_count;
        }
    }

    if(create_file == 'y'){
        //Close output file
        fout.close();
    }

    //End timing and print time taken
    double end = omp_get_wtime();
    const std::chrono::duration<double> time_taken{end - start};
    const auto hrs = std::chrono::duration_cast<std::chrono::hours>(time_taken);
    const auto mins = std::chrono::duration_cast<std::chrono::hours>(time_taken - hrs);
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(time_taken - hrs - mins);
    const auto millisecs = std::chrono::duration_cast<std::chrono::milliseconds>(time_taken - hrs - mins - secs);
    std::cout << "\nTime taken = " << hrs.count() << "h " << mins.count() << "m " << secs.count() << "s " << millisecs.count() << "ms";

    return 0;
}