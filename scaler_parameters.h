#ifndef SCALER_PARAMETERS_H
#define SCALER_PARAMETERS_H

// Feature order (must match model_parameters.h coefficient order exactly):
// 0: PM1.0_ug_m3
// 1: PM2.5_ug_m3
// 2: PM10_ug_m3
// 3: MQ135_CO2_ppm
// 4: MQ135_NH3_ppm
// 5: MQ135_Benzene_ppm
// 6: MQ7_CO_ppm
// 7: MQ7_H2_ppm
// 8: MQ131_O3_ppm
// 9: MQ131_NO2_ppm
// 10: DHT22_Temp_C
// 11: DHT22_Hum_Pct
// 12: Calculated_Heat_Index

const float scaler_mean[] = {
    10.066525,
    25.527783,
    40.707558,
    505.368750,
    25.067175,
    6.204528,
    4.619437,
    8.091087,
    0.059101,
    0.100075,
    24.962050,
    60.184150,
    44.949417
};

const float scaler_std[] = {
    8.808935,
    21.661187,
    34.425011,
    109.862719,
    11.574638,
    3.313796,
    5.010860,
    4.040236,
    0.033778,
    0.054318,
    6.062502,
    17.692962,
    12.750980
};

#endif // SCALER_PARAMETERS_H