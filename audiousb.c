/*
 * audiousb.c - Driver USB para comunicación con Arduino
 *
 * Este módulo de kernel maneja la comunicación USB entre el nodo maestro
 * del clúster y un Arduino que controla una matriz de LEDs 5x5.
 * Se registra como dispositivo de caracteres en /dev/audiousb.
 *
 * Flujo de datos:
 *   Biblioteca -> write() -> Driver (kernel)
 *   -> usb_bulk_msg() -> Bus USB -> Arduino -> Matriz LEDs 5x5
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

//IDs del Arduino - verificar con lsusb 
#define VENDOR_ID            0x1A86
#define PRODUCT_ID           0x7523

#define DEVICE_NAME          "audiousb"
#define BUFFER_SIZE          64
#define AUDIOUSB_FRAME_SIZE  5      // 5 bytes: uno por columna 
#define MAX_LEVEL            5      // Nivel máximo por columna 
#define USB_TIMEOUT          5000   // Timeout en milisegundos 

// Estructura del dispositivo
struct audiousb_device {
    struct usb_device       *udev;
    struct usb_interface    *interface;
    unsigned char           *bulk_out_buffer;
    __u8                    bulk_out_endpoint;
    struct usb_class_driver class;
};

static struct usb_driver audiousb_driver_struct;

/* Tabla de dispositivos USB soportados */
static struct usb_device_id audiousb_table[] = {
    { USB_DEVICE(VENDOR_ID, PRODUCT_ID) },
    {}
};
MODULE_DEVICE_TABLE(usb, audiousb_table);

/* Se ejecuta cuando la biblioteca abre /dev/audiousb */
static int audiousb_open(struct inode *inode, struct file *file)
{
    struct audiousb_device *dev;
    struct usb_interface *interface;
    int subminor;

    subminor = iminor(inode);

    interface = usb_find_interface(&audiousb_driver_struct, subminor);
    if (!interface) {
        pr_err("audiousb: no se encontro interfaz para minor %d\n", subminor);
        return -ENODEV;
    }

    dev = usb_get_intfdata(interface);
    if (!dev)
        return -ENODEV;

    file->private_data = dev;

    pr_info("audiousb: dispositivo abierto\n");
    return 0;
}

/*
 * Se ejecuta cuando la biblioteca escribe al dispositivo.
 * Recibe los 5 niveles del espectrograma y los manda al Arduino por USB.
 */
static ssize_t audiousb_write(struct file *file, const char __user *user_buf,
                               size_t count, loff_t *ppos)
{
    struct audiousb_device *dev;
    int actual_length;
    int ret;
    size_t to_send;

    dev = file->private_data;
    if (!dev)
        return -ENODEV;

    if (count == 0)
        return 0;

    to_send = (count > BUFFER_SIZE) ? BUFFER_SIZE : count;

    /* Copiar datos del espacio de usuario al buffer del kernel */
    if (copy_from_user(dev->bulk_out_buffer, user_buf, to_send))
        return -EFAULT;

    /* Validar niveles del espectrograma */
    if (to_send == AUDIOUSB_FRAME_SIZE) {
        int i;
        for (i = 0; i < AUDIOUSB_FRAME_SIZE; i++) {
            if (dev->bulk_out_buffer[i] > MAX_LEVEL)
                dev->bulk_out_buffer[i] = MAX_LEVEL;
        }
    }

    /* Enviar por USB al Arduino */
    ret = usb_bulk_msg(dev->udev,
                       usb_sndbulkpipe(dev->udev, dev->bulk_out_endpoint),
                       dev->bulk_out_buffer,
                       to_send,
                       &actual_length,
                       USB_TIMEOUT);

    if (ret) {
        pr_err("audiousb: error enviando datos (%d)\n", ret);
        return ret;
    }

    pr_info("audiousb: se enviaron %d bytes al Arduino\n", actual_length);
    return actual_length;
}

/* Se ejecuta cuando la biblioteca cierra el dispositivo */
static int audiousb_release(struct inode *inode, struct file *file)
{
    pr_info("audiousb: dispositivo cerrado\n");
    return 0;
}

static struct file_operations audiousb_fops = {
    .owner   = THIS_MODULE,
    .open    = audiousb_open,
    .write   = audiousb_write,
    .release = audiousb_release,
};

/*
 * El kernel llama a esta función cuando detecta un Arduino
 * con el vendor/product ID que definimos en la tabla.
 */
static int audiousb_probe(struct usb_interface *interface,
                          const struct usb_device_id *id)
{
    struct audiousb_device *dev;
    struct usb_endpoint_descriptor *endpoint;
    int i;
    int ret;

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->udev = usb_get_dev(interface_to_usbdev(interface));
    dev->interface = interface;

    /* Buscar endpoint bulk OUT */
    for (i = 0; i < interface->cur_altsetting->desc.bNumEndpoints; i++) {
        endpoint = &interface->cur_altsetting->endpoint[i].desc;

        if (usb_endpoint_is_bulk_out(endpoint)) {
            dev->bulk_out_endpoint = endpoint->bEndpointAddress;
            break;
        }
    }

    if (!dev->bulk_out_endpoint) {
        pr_err("audiousb: no se encontro endpoint bulk OUT\n");
        kfree(dev);
        return -ENODEV;
    }

    dev->bulk_out_buffer = kzalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!dev->bulk_out_buffer) {
        kfree(dev);
        return -ENOMEM;
    }

    dev->class.name = DEVICE_NAME "%d";
    dev->class.fops = &audiousb_fops;

    ret = usb_register_dev(interface, &dev->class);
    if (ret) {
        pr_err("audiousb: no se pudo registrar el char device\n");
        kfree(dev->bulk_out_buffer);
        kfree(dev);
        return ret;
    }

    usb_set_intfdata(interface, dev);

    pr_info("audiousb: Arduino conectado (minor=%d, endpoint=0x%02x)\n",
            interface->minor, dev->bulk_out_endpoint);
    return 0;
}

/* Se llama cuando desconectan el Arduino */
static void audiousb_disconnect(struct usb_interface *interface)
{
    struct audiousb_device *dev;

    dev = usb_get_intfdata(interface);
    usb_set_intfdata(interface, NULL);

    usb_deregister_dev(interface, &dev->class);

    kfree(dev->bulk_out_buffer);
    usb_put_dev(dev->udev);
    kfree(dev);

    pr_info("audiousb: Arduino desconectado\n");
}

static struct usb_driver audiousb_driver_struct = {
    .name       = DEVICE_NAME,
    .id_table   = audiousb_table,
    .probe      = audiousb_probe,
    .disconnect = audiousb_disconnect,
};

module_usb_driver(audiousb_driver_struct);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver USB - Visualizador de audio con Arduino");