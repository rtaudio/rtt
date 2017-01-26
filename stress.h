namespace rtt {
	
	struct stress {
		int cpu(void);
		int io();
		int vm(long long bytes, long long stride, long long hang, int keep);
		int hdd(long long bytes);
		
		void startFlag();
		void cancel();		
	}
}
	







