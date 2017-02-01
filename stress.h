namespace rtt {

    struct stress {
        static volatile bool s_stress;

        static void runAsync( int startDelayUs);

        static void cpu();

        static void io();

        static bool vm(size_t bytes, size_t stride=4069, bool singleAllocation=false);

        static bool hdd(size_t bytes);


        static inline void startFlag() {
            s_stress = 1;
        }

        static inline void stop() {
            s_stress = 0;
        }
    };
}
	







