/*
Deinterleave FASQ files containing both reads from a paired-end read.
The script makes the following assumption:
    * FASTQ format (4 lines per read), and the two reads (fordward and reverse) in a row.

USAGE: :/deinterleave file.fastq  f.file.fastq r.file.fastq
RESULT: f.file.fastq r.file.fastq containing fordward and backward reads, respectively

Can deinterleave FASTQ files of any size, does not consume memory. The execution time is
I/O bounded.
*/
#include <stdio.h>
#include <stdlib.h>

void print_usage()
{
    printf("USAGE: :/deinterleave file.fastq  f.file.fastq r.file.fastq\n");
    exit(0);
}


int main(int argc, char *argv[])
{
    if(argc != 4) print_usage();
    FILE *fastq = fopen(argv[1], "r");
    FILE *f_fastq = fopen(argv[2], "w");
    FILE *r_fastq = fopen(argv[3], "w");
    if (fastq != NULL && f_fastq != NULL && r_fastq != NULL)
    {
        int count = 0;
        char line[1024];
        while(fgets(line, sizeof line, fastq) != NULL)
        {
            if (count < 4)
            {
                fputs(line, f_fastq);
                count++;
            }
            else
            {
                fputs(line, r_fastq);
                count++;
            }
            if (count == 8)  count = 0;
        }
        fclose(fastq);
        fclose(f_fastq);
        fclose(r_fastq);
    }
    else
    {
        printf("Error reading some of the input files\n");
        exit(0);
    }
    return 0;
}