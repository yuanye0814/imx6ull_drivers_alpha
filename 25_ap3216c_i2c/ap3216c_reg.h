
/* AP3216C register address */
#define AP3216C_SYSTEM_CONFIGURATION    0x00
#define AP3216C_INT_STATUS              0x01
#define AP3216C_INT_CLEAR              0x02
#define AP3216C_IR_DATA_LOW            0x0A
#define AP3216C_IR_DATA_HIGH           0x0B
#define AP3216C_ALS_DATA_LOW           0x0C
#define AP3216C_ALS_DATA_HIGH          0x0D
#define AP3216C_PS_DATA_LOW            0x0E
#define AP3216C_PS_DATA_HIGH           0x0F

/* AP3216C mode control */
#define AP3216C_MODE_POWER_DOWN        0x00
#define AP3216C_MODE_ALS               0x01
#define AP3216C_MODE_PS                0x02
#define AP3216C_MODE_ALS_PS            0x03
#define AP3216C_MODE_SW_RESET          0x04

/* AP3216C system configuration bits */
#define AP3216C_INT_CTRL               0x20
#define AP3216C_INT_MEAN_TIME          0x40
#define AP3216C_GAIN_RANGE             0x80

/* AP3216C interrupt status bits */
#define AP3216C_INT_CLEAR_MANNER       0x02
#define AP3216C_PS_INT_STATUS          0x02
#define AP3216C_ALS_INT_STATUS         0x01

/* AP3216C range */
#define AP3216C_ALS_RANGE_20661       0x00
#define AP3216C_ALS_RANGE_5162        0x01
#define AP3216C_ALS_RANGE_1291        0x02
#define AP3216C_ALS_RANGE_323         0x03

/* AP3216C integration time */
#define AP3216C_ALS_IT_25MS           0x00
#define AP3216C_ALS_IT_50MS           0x01
#define AP3216C_ALS_IT_100MS          0x02
#define AP3216C_ALS_IT_400MS          0x03

/* AP3216C persistence time */
#define AP3216C_ALS_PERSIST_1         0x00
#define AP3216C_ALS_PERSIST_2         0x01
#define AP3216C_ALS_PERSIST_4         0x02
#define AP3216C_ALS_PERSIST_8         0x03