/*
 * audiousb.c - Driver USB para comunicación con Arduino
 * Proyecto 2 - Sistema Distribuido de Análisis de Audio
 * CE 4303 - Principios de Sistemas Operativos
 *
 * Este módulo de kernel maneja la comunicación USB entre el nodo maestro
 * del clúster y un Arduino que controla una matriz de LEDs 7x7.
 * Se registra como dispositivo de caracteres en /dev/audiousb.
 *
 * Flujo de datos:
 *   Escritura: Biblioteca -> write() -> Driver -> usb_bulk_msg() -> Arduino
 *   Lectura:   Arduino -> usb_bulk_msg() -> Driver -> read() -> Biblioteca
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

/* IDs del Arduino - verificar con lsusb */
#define VENDOR_ID            0x1A86
#define PRODUCT_ID           0x7523

#define DEVICE_NAME          "audiousb"
#define BUFFER_SIZE          64
#define AUDIOUSB_FRAME_SIZE  7      /* 7 bytes: uno por columna */
#define MAX_LEVEL            7      /* Nivel máximo por columna (7 filas) */
#define USB_TIMEOUT_WRITE    5000   /* Timeout de escritura en ms */
#define USB_TIMEOUT_READ     2000   /* Timeout de lectura en ms */

/* Estructura del dispositivo */
struct audiousb_device {
    struct usb_device       *udev;
    struct usb_interface    *interface;
    unsigned char           *bulk_out_buffer;
    unsigned char           *bulk_in_buffer;
    __u8                    bulk_out_endpoint;
    __u8                    bulk_in_endpoint;
    int                     bulk_in_size;
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
 * Recibe los 7 niveles del espectrograma y los manda al Arduino por USB.
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

    if (copy_from_user(dev->bulk_out_buffer, user_buf, to_send))
        return -EFAULT;

    /* Validar niveles del espectrograma */
    if (to_send == AUDIOUSB_FRAME_SIZE) {
        int i;
        pr_info("audiousb: frame [%d, %d, %d, %d, %d, %d, %d]\n",
                dev->bulk_out_buffer[0], dev->bulk_out_buffer[1],
                dev->bulk_out_buffer[2], dev->bulk_out_buffer[3],
                dev->bulk_out_buffer[4], dev->bulk_out_buffer[5],
                dev->bulk_out_buffer[6]);
        for (i = 0; i < AUDIOUSB_FRAME_SIZE; i++) {
            if (dev->bulk_out_buffer[i] > MAX_LEVEL)
                dev->bulk_out_buffer[i] = MAX_LEVEL;
        }
    }

    ret = usb_bulk_msg(dev->udev,
                       usb_sndbulkpipe(dev->udev, dev->bulk_out_endpoint),
                       dev->bulk_out_buffer,
                       to_send,
                       &actual_length,
                       USB_TIMEOUT_WRITE);

    if (ret) {
        pr_err("audiousb: error enviando datos (%d)\n", ret);
        return ret;
    }

    pr_info("audiousb: se enviaron %d bytes al Arduino\n", actual_length);
    return actual_length;
}

/*
 * Se ejecuta cuando la biblioteca lee del dispositivo.
 * Lee datos que el Arduino manda de vuelta por USB.
 */
static ssize_t audiousb_read(struct file *file, char __user *user_buf,
                              size_t count, loff_t *ppos)
{
    struct audiousb_device *dev;
    int ret;
    int actual_length;
    int to_read;

    dev = file->private_data;
    if (!dev)
        return -ENODEV;

    if (!dev->bulk_in_endpoint) {
        pr_err("audiousb: no hay endpoint de entrada\n");
        return -ENODEV;
    }

    to_read = (count > BUFFER_SIZE) ? BUFFER_SIZE : count;

    ret = usb_bulk_msg(dev->udev,
                       usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpoint),
                       dev->bulk_in_buffer,
                       to_read,
                       &actual_length,
                       USB_TIMEOUT_READ);

    if (ret) {
        pr_err("audiousb: error leyendo datos (%d)\n", ret);
        return ret;
    }

    if (copy_to_user(user_buf, dev->bulk_in_buffer, actual_length))
        return -EFAULT;

    pr_info("audiousb: se leyeron %d bytes del Arduino\n", actual_length);
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
    .read    = audiousb_read,
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

    /* Buscar endpoints bulk OUT e IN */
    for (i = 0; i < interface->cur_altsetting->desc.bNumEndpoints; i++) {
        endpoint = &interface->cur_altsetting->endpoint[i].desc;

        if (usb_endpoint_is_bulk_out(endpoint) && !dev->bulk_out_endpoint) {
            dev->bulk_out_endpoint = endpoint->bEndpointAddress;
        }

        if (usb_endpoint_is_bulk_in(endpoint) && !dev->bulk_in_endpoint) {
            dev->bulk_in_endpoint = endpoint->bEndpointAddress;
            dev->bulk_in_size = usb_endpoint_maxp(endpoint);
        }
    }

    if (!dev->bulk_out_endpoint) {
        pr_err("audiousb: no se encontro endpoint bulk OUT\n");
        kfree(dev);
        return -ENODEV;
    }

    /* Reservar buffer de salida */
    dev->bulk_out_buffer = kzalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!dev->bulk_out_buffer) {
        kfree(dev);
        return -ENOMEM;
    }

    /* Reservar buffer de entrada si hay endpoint IN */
    if (dev->bulk_in_endpoint) {
        dev->bulk_in_buffer = kzalloc(BUFFER_SIZE, GFP_KERNEL);
        if (!dev->bulk_in_buffer) {
            kfree(dev->bulk_out_buffer);
            kfree(dev);
            return -ENOMEM;
        }
        pr_info("audiousb: endpoint IN encontrado (0x%02x)\n", dev->bulk_in_endpoint);
    }

    dev->class.name = DEVICE_NAME "%d";
    dev->class.fops = &audiousb_fops;

    ret = usb_register_dev(interface, &dev->class);
    if (ret) {
        pr_err("audiousb: no se pudo registrar el char device\n");
        kfree(dev->bulk_in_buffer);
        kfree(dev->bulk_out_buffer);
        kfree(dev);
        return ret;
    }

    usb_set_intfdata(interface, dev);

    pr_info("audiousb: Arduino conectado (minor=%d, out=0x%02x, in=0x%02x)\n",
            interface->minor, dev->bulk_out_endpoint, dev->bulk_in_endpoint);
    return 0;
}

/* Se llama cuando desconectan el Arduino */
static void audiousb_disconnect(struct usb_interface *interface)
{
    struct audiousb_device *dev;

    dev = usb_get_intfdata(interface);
    usb_set_intfdata(interface, NULL);

    usb_deregister_dev(interface, &dev->class);

    kfree(dev->bulk_in_buffer);
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
MODULE_DESCRIPTION("Driver USB - Visualizador de audio con Arduino (CE 4303)");