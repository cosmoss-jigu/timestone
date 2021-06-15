# Note for versioning/

## Stats
### Need to forcely call stat_thread_merge(self) in ts_thread_finish()
 - just tricks, add stat_thread_merge(self) at the end of function to call whenever
 - don't know why it isn't called with tree_bench


