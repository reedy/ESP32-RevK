// VL53L0X control

typedef struct vl53l0x_s vl53l0x_t;

vl53l0x_t *vl53l0x_init(uint8_t port,uint8_t scl,uint8_t sda);
void vl532l0x_end(vl53l0x_t *);
