/*
    SPM Simulator
    - Yooseong Kim 
 */

#include <vector>
#include <stack>

///////////////////////////////////////
// SPM simulation
///////////////////////////////////////
class SPMSim 
{
    private:
        struct rangeType {
            const long long int start;
            const long long int size;

            rangeType(const long long int addr, const long long int sz) : start(addr), size(sz) { }
        };

        // address range - represents main memory address range of a partition
        class AddressRange {
            private:
                std::vector<rangeType> ranges;
                int nRanges;

            public:
                AddressRange();
                ~AddressRange();

                void addRange(const long long int &start, const long long int &size);
                bool isAddrInRange(const Addr addr) const;
        }; 

        struct partType {
            const int size;             // the size of the partition
            const int parent;           // the parent ID (only used when function-splitting is in use)
            const AddressRange* adrg;   // the (main memory) address range where the partition originally is
            const int SPMAddr;          // the SPM addr to which the partition is mapped 
            bool loaded;                // is the function loaded yes(true)/no(false)
            
            partType& operator=(partType p) { return *this;}
            partType(const int sz, const int p, const AddressRange* rg, const int addr) : size(sz), parent(p), adrg(rg), SPMAddr(addr) { loaded = false; }
        };

        // All partitions
        class Partitions {
            private:
                std::vector<partType> pList;
                int nPartitions;

            public:
                Partitions();
                ~Partitions();

                void addPartition(const int size, const int parentID, const AddressRange* adrg, const int SPMAddr);

                int findMatchingPartition(const Addr addr) const;

                void loadPartition(const int pID);
                bool isPartitionLoaded(const int pID) const; 

                int getPartitionSize(const int pID) const;

                int getPartitionParent(const int pID) const;
        };

        Partitions pts;

        // just like call stack. a context is either a function or a partition
        std::stack<int> contextStack;

        long long int userCodeStartAddr;
        long long int userCodeEndAddr;

        // C - instruction execution time (computation time), L - DMA transfer time, M - management cost
        long long int C;
        long long int L;
        long long int M;

        enum endis {enable, disable};
        enum endis mode;

        bool readCodeMappingInput();
        void readUserCodeRange();

        void calculateSPMAccessTime(const Addr addr);
    public:
        SPMSim();
        ~SPMSim();

        void execute(const Addr addr);
};
