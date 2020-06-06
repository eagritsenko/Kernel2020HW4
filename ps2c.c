#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/uaccess.h>
#include <asm/io.h>

MODULE_LICENSE("GPL");





char *k_queue = NULL;
int q_size;
int q_front;
int q_rear;
bool closing;

static DEFINE_SPINLOCK(queue_lock);

static void queue_reader(unsigned long data);

static DECLARE_TASKLET(read_key_queue, queue_reader, 0);

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

static char q_pop(void){
	unsigned long flags;
	char result = 0;
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

static void queue_reader(unsigned long data){
	unsigned int cur = q_pop();
	while(current){
		printk(KERN_DEBUG "Caught keyboard signal: %x\n", cur);
		cur = q_pop();
	}
}

static irqreturn_t ps2_int_handler(int irq, void *dev_id){
	q_push(inb(0x60));
	tasklet_schedule(&read_key_queue);
	return IRQ_HANDLED; 
}


static int __init ps2c_module_init(void){
	init_queue(1024);
	if(request_irq(1, ps2_int_handler, IRQF_SHARED, "ps2c", (void *)ps2_int_handler)){
		printk(KERN_ERR "Requesting IRQ failed");
		free_queue();
		return -1;
	}
	printk(KERN_DEBUG "Init successful");
	return 0;
}


static void __exit ps2c_module_exit(void){
	free_queue();
	free_irq(1, (void *)ps2_int_handler);
	printk(KERN_DEBUG "Exit successful");
}

module_init(ps2c_module_init);
module_exit(ps2c_module_exit);
