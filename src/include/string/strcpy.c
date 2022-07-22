void strcpy(char* p, const char * s)
{
        const char * temp1 = s;
        char * temp2 = p;
        while(*temp1 != '\0')
        {
                *temp2 = *temp1;
                temp1++;
                temp2++;
        }
        *temp2 = '\0';
}

