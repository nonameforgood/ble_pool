uint16_t ReadDS18b20(uint32_t dataPin);



void* BeginDS18b20(uint32_t dataPin);
int32_t ReadDS18b20(void *handle, uint16_t &temp);
void EndDS18b20(void *handle);