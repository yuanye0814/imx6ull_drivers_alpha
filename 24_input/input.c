#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>

#define NAME "input"

#define KEY_COUNT   1
#define KEY_DEBOUNCE_TIME_MS 10

struct key_desc {
    int gpio;
    int irq;
    int keycode;
    char name[16];
    struct timer_list timer;
};

struct key_dev {
    struct input_dev *input;
    struct key_desc keys[KEY_COUNT];
};

static void key_timer_handler(unsigned long data)
{
    struct key_desc *key = (struct key_desc *)data;
    struct key_dev *keydev = container_of(key, struct key_dev, keys[0]);
    int val = gpio_get_value(key->gpio);
    
    input_report_key(keydev->input, key->keycode, !val);
    input_sync(keydev->input);
}

static irqreturn_t key_irq_handler(int irq, void *data)
{
    struct key_desc *key = (struct key_desc *)data;
    
    mod_timer(&key->timer, jiffies + msecs_to_jiffies(KEY_DEBOUNCE_TIME_MS));
    return IRQ_HANDLED;
}

static int key_probe(struct platform_device *pdev)
{
    struct key_dev *keydev;
    int ret, i;

    keydev = devm_kzalloc(&pdev->dev, sizeof(*keydev), GFP_KERNEL);
    if (!keydev)
        return -ENOMEM;
    
    // Allocate input device
    keydev->input = devm_input_allocate_device(&pdev->dev);
    if (!keydev->input)
        return -ENOMEM;

    keydev->input->name = "IMX6ULL Key";
    keydev->input->id.bustype = BUS_HOST;
    
    // Set input capabilities
    __set_bit(EV_KEY, keydev->input->evbit);
    __set_bit(EV_REP, keydev->input->evbit);
    __set_bit(KEY_ENTER, keydev->input->keybit);

    // Initialize keys
    for (i = 0; i < KEY_COUNT; i++) {
        keydev->keys[i].gpio = of_get_named_gpio(pdev->dev.of_node, "key-gpios", i);
        if (keydev->keys[i].gpio < 0) {
            dev_err(&pdev->dev, "Failed to get GPIO %d\n", i);
            return keydev->keys[i].gpio;
        }

        keydev->keys[i].keycode = KEY_ENTER;
        snprintf(keydev->keys[i].name, sizeof(keydev->keys[i].name), "key%d", i);

        ret = devm_gpio_request(&pdev->dev, keydev->keys[i].gpio, keydev->keys[i].name);
        if (ret) {
            dev_err(&pdev->dev, "Failed to request GPIO %d\n", keydev->keys[i].gpio);
            return ret;
        }

        gpio_direction_input(keydev->keys[i].gpio);

        // Setup timer
        init_timer(&keydev->keys[i].timer);
        keydev->keys[i].timer.function = key_timer_handler;
        keydev->keys[i].timer.data = (unsigned long)&keydev->keys[i];

        // Setup IRQ
        keydev->keys[i].irq = gpio_to_irq(keydev->keys[i].gpio);
        ret = devm_request_irq(&pdev->dev, keydev->keys[i].irq, key_irq_handler,
                              IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                              keydev->keys[i].name, &keydev->keys[i]);
        if (ret) {
            dev_err(&pdev->dev, "Failed to request IRQ %d\n", keydev->keys[i].irq);
            return ret;
        }
    }

    // Register input device
    ret = input_register_device(keydev->input);
    if (ret) {
        dev_err(&pdev->dev, "Failed to register input device\n");
        return ret;
    }

    platform_set_drvdata(pdev, keydev);
    dev_info(&pdev->dev, "Key input driver probed successfully\n");
    
    return 0;
}

static int key_remove(struct platform_device *pdev)
{
    struct key_dev *keydev = platform_get_drvdata(pdev);
    int i;
    
    for (i = 0; i < KEY_COUNT; i++) {
        del_timer_sync(&keydev->keys[i].timer);
    }
    
    input_unregister_device(keydev->input);
    return 0;
}

struct of_device_id key_of_match[] = {
    {.compatible = "alpha-key"},
    {},
};

static struct platform_driver key_driver = {
    .driver = {
        .name = "imx6ull-key",
        .of_match_table = key_of_match,
    },
    .probe = key_probe,
    .remove = key_remove,
};

static int __init key_driver_init(void)
{
    printk(NAME " init\n");

    return platform_driver_register(&key_driver);
}

static void __exit key_driver_exit(void)
{
    printk(NAME " exit\n");
    platform_driver_unregister(&key_driver);
}

module_init(key_driver_init);
module_exit(key_driver_exit);

MODULE_AUTHOR("Alvin <yuanye0814@gmail.com>");
MODULE_DESCRIPTION("input - key");
MODULE_LICENSE("GPL");