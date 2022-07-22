int strcmp(const char* a, const char* b){

        while(*a && (*a++ == *b++));

        return  *(const unsigned char*)a - *(const unsigned char*)b;

}

