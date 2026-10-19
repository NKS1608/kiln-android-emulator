// SPDX-License-Identifier: GPL-2.0
/*
 * kiln-cpufreq: the CPU frequencies of the phone a Kiln instance poses as. QEMU's device tree gives each CPU
 * "kiln,max-freq-khz" (Kiln's qemu/patches/0005, from the launcher's CpuLayout); the CPUs of a cluster with the same value
 * share a policy with that one frequency. The vCPUs run at the Mac's speed whatever is set: this only tells the guest
 * (cpufreq sysfs, and apps and engines that pick their cores by it) what the phone would show.
 */
#include <linux/arch_topology.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>

static u32 kiln_khz(unsigned int cpu)
{
	struct device_node *np = of_cpu_device_node_get(cpu);
	u32 khz = 0;

	if (np) {
		of_property_read_u32(np, "kiln,max-freq-khz", &khz);
		of_node_put(np);
	}
	return khz;
}

static int kiln_cpufreq_init(struct cpufreq_policy *policy)
{
	struct cpufreq_frequency_table *table;
	u32 khz = kiln_khz(policy->cpu);
	unsigned int cpu;

	if (!khz)
		return -ENODEV;
	for_each_cpu(cpu, topology_cluster_cpumask(policy->cpu))
		if (kiln_khz(cpu) == khz)
			cpumask_set_cpu(cpu, policy->cpus);
	table = kcalloc(2, sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;
	table[0].frequency = khz;
	table[1].frequency = CPUFREQ_TABLE_END;
	policy->freq_table = table;
	policy->cur = khz;
	return 0;
}

static void kiln_cpufreq_exit(struct cpufreq_policy *policy)
{
	kfree(policy->freq_table);
}

/* one frequency: nothing to switch */
static int kiln_cpufreq_target_index(struct cpufreq_policy *policy, unsigned int index)
{
	return 0;
}

static unsigned int kiln_cpufreq_get(unsigned int cpu)
{
	return kiln_khz(cpu);
}

static struct cpufreq_driver kiln_cpufreq_driver = {
	.name = "kiln-cpufreq",
	.verify = cpufreq_generic_frequency_table_verify,
	.target_index = kiln_cpufreq_target_index,
	.get = kiln_cpufreq_get,
	.init = kiln_cpufreq_init,
	.exit = kiln_cpufreq_exit,
	.attr = cpufreq_generic_attr,
};

static int __init kiln_cpufreq_module_init(void)
{
	if (!kiln_khz(0))
		return -ENODEV;  /* no layout from the host (an instance of a custom device, or an older Kiln) */
	return cpufreq_register_driver(&kiln_cpufreq_driver);
}

static void __exit kiln_cpufreq_module_exit(void)
{
	cpufreq_unregister_driver(&kiln_cpufreq_driver);
}

module_init(kiln_cpufreq_module_init);
module_exit(kiln_cpufreq_module_exit);
MODULE_DESCRIPTION("Kiln: the posed phone's CPU frequencies, from the device tree");
MODULE_LICENSE("GPL");
