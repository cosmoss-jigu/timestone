/*
 * Generate/analyze pareto/zipf distributions to better understand
 * what an access pattern would look like.
 *
 * For instance, the following would generate a zipf distribution
 * with theta 1.2, using 262144 (1 GiB / 4096) values and split the
 * reporting into 20 buckets:
 *
 *	./t/fio-genzipf -t zipf -i 1.2 -g 1 -b 4096 -o 20
 *
 * Only the distribution type (zipf or pareto) and spread input need
 * to be given, if not given defaults are used.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "zipf.h"
#include "flist.h"
#include "hash.h"

#define DEF_NR_OUTPUT	20

struct node {
	struct flist_head list;
	unsigned long long val;
	unsigned long hits;
};

static struct flist_head *hash;
static unsigned long hash_bits = 24;
static unsigned long hash_size = 1 << 24;

enum {
	TYPE_NONE = 0,
	TYPE_ZIPF,
	TYPE_PARETO,
	TYPE_NORMAL,
};
static const char *dist_types[] = { "None", "Zipf", "Pareto", "Normal" };

enum {
	OUTPUT_NORMAL,
	OUTPUT_CSV,
};

static int dist_type = TYPE_ZIPF;
static unsigned long gib_size = 1;
static unsigned long block_size = 1;
static unsigned long output_nranges = DEF_NR_OUTPUT;
static double percentage;
static double dist_val;
static int output_type = OUTPUT_NORMAL;

#define DEF_ZIPF_VAL	1.2

static unsigned int hashv(unsigned long long val)
{
	return jhash(&val, sizeof(val), 0) & (hash_size - 1);
}

static struct node *hash_lookup(unsigned long long val)
{
	struct flist_head *l = &hash[hashv(val)];
	struct flist_head *entry;
	struct node *n;

	flist_for_each(entry, l) {
		n = flist_entry(entry, struct node, list);
		if (n->val == val)
			return n;
	}

	return NULL;
}

static void hash_insert(struct node *n, unsigned long long val)
{
	struct flist_head *l = &hash[hashv(val)];

	n->val = val;
	n->hits = 1;
	flist_add_tail(&n->list, l);
}

struct output_sum {
	double output;
	unsigned int nranges;
};

static int node_cmp(const void *p1, const void *p2)
{
	const struct node *n1 = p1;
	const struct node *n2 = p2;

	return n2->hits - n1->hits;
}

static void output_csv(struct node *nodes, unsigned long nnodes)
{
	unsigned long i;

	printf("rank, count\n");
	for (i = 0; i < nnodes; i++)
		printf("%lu, %lu\n", i, nodes[i].hits);
}

static void output_normal(struct node *nodes, unsigned long nnodes,
			  unsigned long nranges)
{
	unsigned long i, j, cur_vals, interval_step, next_interval, total_vals;
	unsigned long blocks = percentage * nnodes / 100;
	double hit_percent_sum = 0;
	unsigned long long hit_sum = 0;
	double perc, perc_i;
	struct output_sum *output_sums;

	interval_step = (nnodes - 1) / output_nranges + 1;
	next_interval = interval_step;
	output_sums = malloc(output_nranges * sizeof(struct output_sum));

	for (i = 0; i < output_nranges; i++) {
		output_sums[i].output = 0.0;
		output_sums[i].nranges = 0;
	}

	j = total_vals = cur_vals = 0;

	for (i = 0; i < nnodes; i++) {
		struct output_sum *os = &output_sums[j];
		struct node *node = &nodes[i];
		cur_vals += node->hits;
		total_vals += node->hits;
		os->nranges += node->hits;
		if (i == (next_interval) -1 || i == nnodes - 1) {
			os->output = (double) cur_vals / (double) nranges;
			os->output *= 100.0;
			cur_vals = 0;
			next_interval += interval_step;
			j++;
		}

		if (percentage) {
			if (total_vals >= blocks) {
				double cs = (double) i * block_size / (1024.0 * 1024.0);
				char p = 'M';

				if (cs > 1024.0) {
					cs /= 1024.0;
					p = 'G';
				}
				if (cs > 1024.0) {
					cs /= 1024.0;
					p = 'T';
				}

				printf("%.2f%% of hits satisfied in %.3f%cB of cache\n", percentage, cs, p);
				percentage = 0.0;
			}
		}
	}

	perc_i = 100.0 / (double)output_nranges;
	perc = 0.0;

	printf("\n   Rows           Hits %%         Sum %%           # Hits          Size\n");
	printf("-----------------------------------------------------------------------\n");
	for (i = 0; i < output_nranges; i++) {
		struct output_sum *os = &output_sums[i];
		double gb = (double)os->nranges * block_size / 1024.0;
		char p = 'K';

		if (gb > 1024.0) {
			p = 'M';
			gb /= 1024.0;
		}
		if (gb > 1024.0) {
			p = 'G';
			gb /= 1024.0;
		}

		perc += perc_i;
		hit_percent_sum += os->output;
		hit_sum += os->nranges;
		printf("%s %6.2f%%\t%6.2f%%\t\t%6.2f%%\t\t%8u\t%6.2f%c\n",
			i ? "|->" : "Top", perc, os->output, hit_percent_sum,
			os->nranges, gb, p);
	}

	printf("-----------------------------------------------------------------------\n");
	printf("Total\t\t\t\t\t\t%8llu\n", hit_sum);
	free(output_sums);
}

int main(int argc, char *argv[])
{
	unsigned long offset;
	unsigned long long nranges;
	unsigned long nnodes;
	struct node *nodes;
	struct zipf_state zs;
	int i, j;
	double	arg_val1;

//	nranges = gib_size * 1024ULL;
	nranges = gib_size * 10240ULL;
	nranges /= block_size;
	
	dist_val = 0.0;
	printf("dist_val:%lf\n", dist_val);
	arg_val1 = atof(argv[1]);
	printf("arg0:%s arg1:%s argval:%lf\n", argv[0], argv[1], arg_val1);
	
	//zipf_init(&zs, nranges, dist_val, 1);
	zipf_init(&zs, nranges, arg_val1, 1);

	hash_bits = 0;
	hash_size = nranges;
	while ((hash_size >>= 1) != 0)
		hash_bits++;

	hash_size = 1 << hash_bits;

	hash = calloc(hash_size, sizeof(struct flist_head));
	for (i = 0; i < hash_size; i++)
		INIT_FLIST_HEAD(&hash[i]);

	nodes = malloc(nranges * sizeof(struct node));

	for (i = j = 0; i < nranges; i++) {
		struct node *n;

		offset = zipf_next(&zs);
		printf ("%lu\n", offset);
		
		n = hash_lookup(offset);
		if (n)
			n->hits++;
		else {
			hash_insert(&nodes[j], offset);
			j++;
		}
	}

	qsort(nodes, j, sizeof(struct node), node_cmp);
	nnodes = j;

	if (output_type == OUTPUT_CSV)
		output_csv(nodes, nnodes);
	else
		output_normal(nodes, nnodes, nranges);

	free(hash);
	free(nodes);
	return 0;
}
