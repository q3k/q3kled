#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/fb.h>

struct q3kled_device {
    void __iomem *mem;
    struct device *dev;
    struct fb_info info;
    u32 pseudo_palette[16];
};

static int q3kled_remove(struct platform_device *pdev)
{
    struct q3kled_device *qdev = platform_get_drvdata(pdev);
    unregister_framebuffer(&qdev->info);
    return 0;
}

static int q3kled_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
        u_int transp, struct fb_info *info)
{
    u32 *pal = info->pseudo_palette;
    u32 cr = red >> (16 - info->var.red.length);
    u32 cg = green >> (16 - info->var.green.length);
    u32 cb = blue >> (16 - info->var.blue.length);
    u32 value;

    if (regno >= 16)
        return -EINVAL;

    value = (cr << info->var.red.offset) |
        (cg << info->var.green.offset) |
        (cb << info->var.blue.offset);
    if (info->var.transp.length > 0) {
        u32 mask = (1 << info->var.transp.length) - 1;
        mask <<= info->var.transp.offset;
        value |= mask;
    }
    pal[regno] = value;

    return 0;
}

static struct fb_ops q3kled_ops = {
    .owner = THIS_MODULE,
    .fb_setcolreg = q3kled_setcolreg,
    .fb_fillrect = sys_fillrect,
    .fb_copyarea = sys_copyarea,
    .fb_imageblit = sys_imageblit
};

static int q3kled_probe(struct platform_device *pdev)
{
    struct q3kled_device *qdev;
    struct fb_var_screeninfo *var;
    struct fb_fix_screeninfo *fix;
    struct resource *res;
    unsigned long long i;
    int ret;

    qdev = devm_kzalloc(&pdev->dev, sizeof(*qdev), GFP_KERNEL);
    if (!qdev)
        return -ENOMEM;
    qdev->dev = &(pdev->dev);

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    qdev->mem = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(qdev->mem))
        return PTR_ERR(qdev->mem);

    platform_set_drvdata(pdev, qdev);

    qdev->info.fbops = &q3kled_ops;
    qdev->info.device = &pdev->dev;
    qdev->info.par = qdev;

    var = &qdev->info.var;
    fix = &qdev->info.fix;

    var->accel_flags = FB_ACCEL_NONE;
    var->activate = FB_ACTIVATE_NOW;
    var->xres = 128;
    var->yres = 128;
    var->xres_virtual = var->xres;
    var->yres_virtual = var->yres;
    var->bits_per_pixel = 32;
    var->pixclock = KHZ2PICOS(51200);
    var->vmode = FB_VMODE_NONINTERLACED;
        
    var->transp.offset = 24;
    var->transp.length = 8;
    var->red.offset = 16;
    var->red.length = 8;
    var->green.offset = 8;
    var->green.length = 8;
    var->blue.offset = 0;
    var->blue.length = 8;

    var->hsync_len = 1;
    var->left_margin = 1;
    var->right_margin = 1;
    var->vsync_len = 1;
    var->upper_margin = 1;
    var->lower_margin = 1;

    strcpy(fix->id, "q3kled-fb");
    fix->line_length = var->xres * (var->bits_per_pixel/8);
    fix->smem_len = fix->line_length * var->yres;
    fix->type = FB_TYPE_PACKED_PIXELS;
    fix->visual = FB_VISUAL_TRUECOLOR;

    fix->smem_start = res->start;
    qdev->info.screen_base = qdev->mem;
    qdev->info.pseudo_palette = qdev->pseudo_palette;

    dev_info(&pdev->dev, "led virt=%p phys=%x size=%d\n",
                    qdev->mem, res->start, fix->smem_len);


    for (i = 0; i < 128*128; i++) {
        iowrite32(0x0, qdev->mem + i*4);
    }

    ret = fb_alloc_cmap(&qdev->info.cmap, 256, 0);
    if (ret) {
        dev_err(&pdev->dev, "fb_alloc_cmap failed\n");
        return ret;
    }

    ret = register_framebuffer(&qdev->info);
    if (ret) {
        dev_err(&pdev->dev, "Framebuffer registration failed\n");
        return ret;
    }


    dev_info(&pdev->dev, "All good! %08x\n", ioread32(qdev->mem));
    return 0;
}

static const struct of_device_id q3kled_of_match[] = {
    { .compatible = "xlnx,ledcontroller-1.0",},
    {}
};
MODULE_DEVICE_TABLE(of, q3kled_of_match);

static struct platform_driver q3kled_driver = {
    .driver = {
        .name = "ledcontroller",
        .of_match_table = q3kled_of_match,
    },
    .probe = q3kled_probe,
    .remove = q3kled_remove,
};
module_platform_driver(q3kled_driver);

MODULE_LICENSE("Dual BSD/GPL");
