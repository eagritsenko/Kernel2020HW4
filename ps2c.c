#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <asm/io.h>

MODULE_LICENSE("GPL");


#define MSEC_TIME_INTERVAL 60000


static unsigned char *k_queue = NULL;
static int q_size;
static int q_front;
static int q_rear;
static bool closing;
static DEFINE_SPINLOCK(queue_lock);

static void queue_reader(unsigned long data);
static DECLARE_TASKLET(read_key_queue, queue_reader, 0);

static atomic_t symbol_count = ATOMIC_INIT(0);

static struct timer_list print_timer;
static unsigned int j_period;

static void init_queue(int size){
       q_size = size;
       q_front = 0;
       q_rear = 0;
       closing = true;
       k_queue = kmalloc(size, GFP_KERNEL);
}

static void free_queue(void){
	kfree(k_queue);
	k_queue = NULL;
}

static void q_push(char symbol){
       unsigned long flags;
       spin_lock_irqsave(&queue_lock, flags);
       if(unlikely(q_front == q_rear && !closing))
	       goto exit;
       k_queue[q_rear] = symbol;
       q_rear++;
       if(unlikely(q_rear == q_size))
	       q_rear = 0;
       closing = false;

exit:
       spin_unlock_irqrestore(&queue_lock, flags);
       return;
}

static unsigned char q_pop(void){
	unsigned long flags;
	unsigned char result = 0;
	spin_lock_irqsave(&queue_lock, flags);
	if(unlikely(q_front == q_rear && closing))
		goto exit;
	result = k_queue[q_front];
	q_front++;
	if(unlikely(q_front == q_size))
		q_front = 0;
	closing = true;
exit:
	spin_unlock_irqrestore(&queue_lock, flags);
	return result;
}

static void ct0_adapter(unsigned char code){
	static void *cdt_jump = &&s0;
	goto *cdt_jump;

s0:
	if((0x02 < code && code < 0x36 && code != 0x1D && code != 0x0E && code != 0x2A) ||
			(0x46 < code && code < 0x54) ||
			code == 0x37 ||
			code == 0x39)
	{
		atomic_inc(&symbol_count);
	}
	else if(code == 0xE0)
		cdt_jump = &&s1;
	else if(code == 0xE1)
		cdt_jump = &&pause_s1;
	return;

s1:
	if(code == 0x2A)
		cdt_jump = &&prt_sc_s2;
	else if(code == 0x1C || code == 0x35){
		atomic_inc(&symbol_count);
		cdt_jump = &&s0;
	}
	else
		cdt_jump = &&s0;
	return;

prt_sc_s2:
	cdt_jump = &&prt_sc_s3;
	return;

prt_sc_s3:
	cdt_jump = &&s0;
	return;


pause_s1:
	cdt_jump = &&pause_s2;
	return;

pause_s2:
	cdt_jump = &&pause_s3;
	return;

pause_s3:
	cdt_jump = &&pause_s4;
	return;

pause_s4:
	cdt_jump = &&s0;
	return;
}

static void queue_reader(unsigned long data){
	unsigned char cur = q_pop();
	while(cur){
		ct0_adapter(cur);
		cur = q_pop();
	}
}

static irqreturn_t ps2_int_handler(int irq, void *dev_id){
	q_push(inb(0x60));
	tasklet_schedule(&read_key_queue);
	return IRQ_HANDLED; 
}

static void print_count(struct timer_list *t){
	mod_timer(&print_timer, jiffies +  j_period);
	printk(KERN_INFO "Symbols last time period: %d\n", atomic_xchg(&symbol_count, 0));
}

static int __init ps2c_module_init(void){
	init_queue(1024);
	if(request_irq(1, ps2_int_handler, IRQF_SHARED, "ps2c", (void *)ps2_int_handler)){
		printk(KERN_ERR "Requesting IRQ failed\n");
		free_queue();
		return -1;
	}
	j_period = msecs_to_jiffies(MSEC_TIME_INTERVAL);
	timer_setup(&print_timer, print_count, 0);
	mod_timer(&print_timer, jiffies + j_period);
	printk(KERN_DEBUG "Init successful\n");
	return 0;
}


static void __exit ps2c_module_exit(void){
	free_queue();
	free_irq(1, (void *)ps2_int_handler);
	del_timer(&print_timer);
}

module_init(ps2c_module_init);
module_exit(ps2c_module_exit);
