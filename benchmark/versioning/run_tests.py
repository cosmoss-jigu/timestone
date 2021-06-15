import time
import sys
import os, subprocess
import optparse
from threading import Timer

IS_NUMA = 0
IS_2_SOCKET = 0
IS_PERF = 0

CMD_PARAMS = '-d%d -u%d -i%d -r%d -z%lf -n%d'

PERF_FILE = "__perf_output.file"
CMD_PREFIX_PERF = "perf stat -d -o %s" % (PERF_FILE,)

CMD_BASE_HARRIS = './benchmark_tree_harris'
CMD_BASE_HARRIS_LIST = './benchmark_list_harris'
CMD_BASE_HP_HARRIS = './benchmark_tree_hp_harris'
CMD_BASE_RCU = './benchmark_tree_rcu'
CMD_BASE_RCU_LIST = './benchmark_list_rcu'
CMD_BASE_RLU = './benchmark_tree_rlu'
CMD_BASE_RLU_LIST = './benchmark_list_rlu'
CMD_BASE_RLU_ORDO_LIST = './benchmark_list_rlu_ordo'
CMD_BASE_VLIST = './benchmark_tree_vtree'
CMD_BASE_VLIST_LIST = './benchmark_list_vlist'
CMD_BASE_VRBTREE = './benchmark_tree_vrbtree'
CMD_BASE_RLU_ORDO = './benchmark_tree_rlu_ordo'
CMD_BASE_MVRLU = './benchmark_tree_mvrlu'
CMD_BASE_MVRLU_S = './benchmark_tree_mvrlu_S'
CMD_BASE_MVRLU_L = './benchmark_tree_mvrlu_L'
CMD_BASE_MVRLU_ORDO = './benchmark_tree_mvrlu_ordo'
CMD_BASE_MVRLU_ORDO_LIST = './benchmark_list_timestone'
CMD_BASE_CITRUS_MVRLU_ORDO = './benchmark_tree_citrus_mvrlu_ordo'
CMD_BASE_SWISSTM = './benchmark_tree_swisstm'
CMD_BASE_SWISSTM_LIST = './benchmark_list_swisstm'
CMD_BASE_BONSAI = './benchmark_tree_bonsai'
CMD_BASE_PRCU_D = './benchmark_tree_prcu_d'

OUTPUT_FILENAME = '___temp.file'

W_OUTPUT_FILENAME = '__w_check.txt'

CMD_PREFIX_LIBS = 'export LD_PRELOAD=\\"/usr/lib64/libtcmalloc_minimal.so.4\\"'
#CMD_PREFIX_LIBS = 'export LD_PRELOAD=\\"$LD_PRELOAD\\"'

LLIST_SCRIPT = 'plot_scripts/llist'
HLIST_SCRIPT = 'plot_scripts/hlist'

CMD_NUMA_BIND_TO_CPU_0 = 'numactl --cpunodebind=0 '
CMD_NUMA_BIND_TO_CPU_1 = 'numactl --cpunodebind=1 '
CMD_NUMA_BIND_TO_CPU_0_1 = 'numactl --cpunodebind=0,1 '
CMD_NUMA_PREFIX_8  = 'taskset -c 0-7 '
CMD_NUMA_PREFIX_10 = 'taskset -c 0-9 '
CMD_NUMA_PREFIX_12 = 'taskset -c 0-11 '
CMD_NUMA_PREFIX_14 = 'taskset -c 0-13 '
CMD_NUMA_PREFIX_15 = 'taskset -c 0-14 '
CMD_NUMA_PREFIX_16 = 'taskset -c 0-15 '
CMD_NUMA_PREFIX_30 = 'taskset -c 0-29 '
CMD_NUMA_PREFIX_45 = 'taskset -c 0-44 '
CMD_NUMA_PREFIX_60 = 'taskset -c 0-59 '
CMD_NUMA_PREFIX_75 = 'taskset -c 0-74 '
CMD_NUMA_PREFIX_90 = 'taskset -c 0-89 '
CMD_NUMA_PREFIX_105= 'taskset -c 0-104 '
CMD_NUMA_PREFIX_120= 'taskset -c 0-119 '

CMD_BASE = {
	'harris' : CMD_BASE_HARRIS,
	'harris_list' : CMD_BASE_HARRIS_LIST,
	'hp_harris' : CMD_BASE_HP_HARRIS,
	'rcu' : CMD_BASE_RCU,
	'rlu' : CMD_BASE_RLU,
	'rcu_list' : CMD_BASE_RCU_LIST,
	'rlu_list' : CMD_BASE_RLU_LIST,
	'rlu_ordo_list' : CMD_BASE_RLU_ORDO_LIST,
        'vlist' : CMD_BASE_VLIST,
        'vlist_list' : CMD_BASE_VLIST_LIST,
        'vrbtree' : CMD_BASE_VRBTREE,
        'rlu_ordo' : CMD_BASE_RLU_ORDO,
	'mvrlu' : CMD_BASE_MVRLU,
	'mvrlu_ser' : CMD_BASE_MVRLU_S,
	'mvrlu_lin' : CMD_BASE_MVRLU_L,
        'mvrlu_ordo' : CMD_BASE_MVRLU_ORDO,
        'timestone_list' : CMD_BASE_MVRLU_ORDO_LIST,
        'citrus_mvrlu_ordo' : CMD_BASE_CITRUS_MVRLU_ORDO,
        'swisstm' : CMD_BASE_SWISSTM,
        'swisstm_list' : CMD_BASE_SWISSTM_LIST,
        'bonsai' : CMD_BASE_BONSAI,
        'prcu_d' : CMD_BASE_PRCU_D,
}

result_keys = [
	'ops:',
#        'abort_ratio:',
#	'#update ops   :',
#	't_writer_writebacks =',
#	't_writeback_q_iters =',
#	'a_writeback_q_iters =',
#	't_pure_readers =',
#	't_steals =',
#	't_aborts =',
#	't_sync_requests =',
#	't_sync_and_writeback =',
]

perf_result_keys = [
	'instructions              #',
	'branches                  #',
	'branch-misses             #',
	'L1-dcache-loads           #',
	'L1-dcache-load-misses     #',
]

def cmd_numa_prefix(threads_num):
	if (IS_2_SOCKET):
		if (threads_num <= 36):
	                print 'cmd_numa_prefix: BIND_CPU th_num = %d' % (threads_num,)
			return CMD_NUMA_BIND_TO_CPU_1
		
		return CMD_NUMA_BIND_TO_CPU_0_1
	
        print 'cmd_numa_prefix: th_num = %d' % (threads_num)
	
	if (threads_num <= 15):
		return CMD_NUMA_PREFIX_15
	
	if (threads_num <= 30):
		return CMD_NUMA_PREFIX_30

	if (threads_num <= 45):
		return CMD_NUMA_PREFIX_45

	if (threads_num <= 60):
		return CMD_NUMA_PREFIX_60

	if (threads_num <= 75):
		return CMD_NUMA_PREFIX_75
	
        if (threads_num <= 90):
		return CMD_NUMA_PREFIX_90

        if (threads_num <= 105):
		return CMD_NUMA_PREFIX_105
        
        if (threads_num <= 120):
		return CMD_NUMA_PREFIX_120

	print 'cmd_numa_prefix: ERROR th_num = %d' % (threads_num,)


def extract_data(output_data, key_str):
	data = output_data.split(key_str)[1].split()[0].strip()
	
	if (data.find('nan') != -1):
		return 0
	
	if (key_str == 'L1-dcache-load-misses     #') or (key_str == 'branch-misses             #'):
		data = data.strip('%')
			
	return float(data)

	
def extract_keys(output_data):
	d = {}
	
	for key in result_keys:
		d[key] = extract_data(output_data, key)
	
	if IS_PERF:
		for key in perf_result_keys:
			d[key] = extract_data(output_data, key)
			
	return d


def print_keys(dict_keys):
	print '================================='
	for key in result_keys:
		print '%s %.2f' % (key, dict_keys[key])
	
	if IS_PERF:
		for key in perf_result_keys:
			print '%s %.2f' % (key, dict_keys[key])
	

def run_test(runs_per_test, alg_type, cmd):
  
	ops_total = 0
	total_operations = 0
	aborts_total = 0
	total_combiners = 0
	total_num_of_waiting = 0
	total_additional_readers = 0
	total_additional_writers = 0

	cmd_prefix = 'timeout 300 bash -c "'
	if (IS_PERF):
		cmd_prefix += CMD_PREFIX_PERF + ' '
	
	full_cmd = cmd_prefix + cmd + ' && echo $? > done"'
	
	print full_cmd
	
	total_dict_keys = {}
	for key in result_keys:
		total_dict_keys[key] = 0
	
        i = 0
	while True:
		print 'run %d ' % (i,)
		
		
		if (IS_PERF):
			try:
				os.unlink(PERF_FILE)
			except OSError:
				pass
		
                os.system("rm -rf done")
		os.system('w >> %s' % (W_OUTPUT_FILENAME,))
		time.sleep(1)
                os.system(full_cmd + '>' +  OUTPUT_FILENAME)
		os.system('w >> %s' % (W_OUTPUT_FILENAME,))

                if os.path.exists("./done") == False:
                    continue
                done = open("done", 'rb')
                rv = done.read()
                print rv
                if rv == '-11':
                    done.close()
                    os.system("rm -rf done")
                    time.sleep(5)
                    continue
                done.close()
                os.system("rm -rf done")


		time.sleep(1)
		f = open(OUTPUT_FILENAME, 'rb')
		output_data = f.read()
		f.close()
	        os.unlink(OUTPUT_FILENAME)

		if (IS_PERF):
			f = open(PERF_FILE, 'rb');
			output_data += f.read()
			f.close()
			os.unlink(PERF_FILE)
		
		print "------------------------------------"
		print output_data
		print "------------------------------------"
				
		dict_keys = extract_keys(output_data)

		print '================================='
		for key in dict_keys.keys():
			total_dict_keys[key] += dict_keys[key]
			
		print_keys(dict_keys)
                i = i + 1
                if i == runs_per_test:
                    break

	for key in total_dict_keys.keys():
		total_dict_keys[key] /= runs_per_test
 	
	return total_dict_keys

def print_run_results(f_out,  update_ratio, th_num, dict_keys):
	
	f_out.write('\n %.2f %.2f' % ( update_ratio, th_num));
	
	for key in result_keys:
		f_out.write(' %.2f' % dict_keys[key])
	
	if IS_PERF:
		for key in perf_result_keys:
			f_out.write(' %.2f' % dict_keys[key])
	
	f_out.flush()

	
def execute(runs_per_test, 
			duration, 
			alg_type, 
			update_ratio, 
			initial_size, 
			range_size, 
			output_filename, 
			zipf_dist_val,
			th_num_list):
  
	
	f_w = open(W_OUTPUT_FILENAME, 'wb');
	f_w.close()

	f_out = open(output_filename, 'wb')
        if(alg_type == 'swisstm'):
            result_keys.append("Abort:")
        else:
            if 'abort_ratio:' in result_keys:
                result_keys.remove('abort_ratio:')
            result_keys.append("abort_ratio:")

        if alg_type == 'mvrlu' or alg_type == 'mvrlu_ser' or alg_type == 'mvrlu_lin':
            if '     n_tvlog_cpobj_written_bytes =' in result_keys:
                result_keys.remove('     n_tvlog_cpobj_written_bytes =')
            if '           n_alloc_act_obj_bytes =' in result_keys:
                result_keys.remove('           n_alloc_act_obj_bytes =')
            if '              n_tvlog_ckpt_bytes =' in result_keys:
                result_keys.remove('              n_tvlog_ckpt_bytes =')
            if '           n_oplog_written_bytes =' in result_keys:
                result_keys.remove('           n_oplog_written_bytes =')
            if '       n_ckptlog_writeback_bytes =' in result_keys:
                result_keys.remove('       n_ckptlog_writeback_bytes =')

            result_keys.append('     n_tvlog_cpobj_written_bytes =')
            result_keys.append('           n_alloc_act_obj_bytes =')
            result_keys.append('              n_tvlog_ckpt_bytes =')
            result_keys.append('           n_oplog_written_bytes =')
            result_keys.append('       n_ckptlog_writeback_bytes =')
 
	
	cmd_header = '[%s] ' % (alg_type,) + CMD_BASE[alg_type] + ' ' + CMD_PARAMS % (
		duration,
		update_ratio, 
		initial_size,
		range_size,	
		zipf_dist_val,
		0)
		
	f_out.write(cmd_header + '\n')
	f_out.flush()
	
	results = []
	for th_num in th_num_list:
		
		
		cmd = CMD_BASE[alg_type] + ' ' + CMD_PARAMS % (
			duration,
			update_ratio, 
			initial_size,
			range_size,	
			zipf_dist_val,
			th_num)
		
			
		print '-------------------------------'
		print '[%d] %s ' % (th_num, cmd)
                if(IS_NUMA):
                    cmd = cmd_numa_prefix(th_num) + cmd

		dict_keys = run_test(runs_per_test, alg_type, cmd)

		results.append(dict_keys)

		print_run_results(f_out, update_ratio, th_num, dict_keys)

	
	f_out.write('\n\n')
	f_out.flush()
	f_out.close()
        del result_keys[-1]
	
	
	print 'DONE: written output to %s' % (output_filename,)


if '__main__' == __name__:


    parser = optparse.OptionParser()
    parser.add_option("-k", "--runs", type = 'int', default = 1,
                    help = "runs per test")
    parser.add_option("-w", "--rlu-max-ws", type = 'int', default = 1,
                    help = "Maximum rlu write set aggregated in RLU defferal")
    parser.add_option("-b", "--buckets", type = 'int', default = 1,
                    help = "Number of buckets")
    parser.add_option("-d", "--duration", type = 'int', default = 5000,
                    help = "Duration of run in ms")
    parser.add_option("-t", "--alg-type", default = "rlu",
                    help = "type of algorithm")
    parser.add_option("-u", "--update-rate", type = 'int', default = 0,
                    help = "Percentage of update transaction: 0-1000(100%)")
    parser.add_option("-i", "--initial_size", type = 'int', default =1000,
                    help = "Number of elements to insert before test")
    parser.add_option("-r", "--range_size", type = 'int', default = 2000,
                    help = "range size")
    parser.add_option("-z", "--zipf", type = 'float', default = 0,
                    help = "zipf dist value")
    parser.add_option("-n", "--th_num_list", type = 'int', action = "append",
                    dest = 'th_num_list', default = [], help = "Number of threads")
    parser.add_option("-f", "--output-filename", default = "__result.txt",
                    help = "Output Filename")
    parser.add_option("-g", "--generate-graph", type = 'int', default = 0,
                    help = "Generate graph")


    (opts, args) = parser.parse_args()

    #check options
    for opt in vars(opts):
        val = getattr(opts, opt)
        if val == None:
            print("Missing options: %s" %opt)
            parser.print_help()
            exit(1)
        if len(opts.th_num_list) == 0:
            print("Number of threads required\n")
            parser.print_help()
            exit(1)

    result_dir = "./results/"
    llist = False

    if opts.buckets == 1:
        result_dir = result_dir + "llist/" + "u" + str(opts.update_rate) + "/"
        llist = True
    else:
        result_dir = result_dir + "hlist/" + "u" + str(opts.update_rate) + "/"


    try:
        os.stat(result_dir)
    except:
        os.makedirs(result_dir)
        #copy plot scripts to the folder
        if llist == True:
            cmd = 'cp %s/* %s' %(LLIST_SCRIPT, result_dir)
            print cmd
            status = subprocess.check_output(cmd, shell=True)
        else:
            cmd = 'cp %s/* %s' %(HLIST_SCRIPT, result_dir)
            status = subprocess.check_output(cmd, shell=True)

    output_filename = result_dir + opts.output_filename
    execute(opts.runs, opts.rlu_max_ws, opts.buckets, opts.duration,
            opts.alg_type, opts.update_rate, opts.initial_size, opts.range_size,
            output_filename, opts.zipf_dist_val, opts.th_num_list)
    if opts.generate_graph == 1:
        cmd= 'cd %s && sh extr_txs_tree.sh && cd -'%result_dir
        print cmd
        status = subprocess.check_output(cmd, shell=True)

