/*
Copyright (c) 2011 Expression Analysis / Erik Aronesty

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "fastq-lib.h"

/*

See "void usage" below for usage.

*/

void usage(FILE *f);
int debug=0;

int main (int argc, char **argv) {
	char c;
	int mismatch = 0;
	char *in[3] = {0,0,0};
	char *out[5];
	char *orep=NULL;
	int out_n = 0;
	int in_n = 0;
	int threads = 1;				// not really necessary
	char verify='\0';

	int i;
	int mino = 6;
	int pctdiff = 20;				// this number tested well on exome data... tweak for best results
	bool omode = false;	
	char *bfil = NULL;

	while (	(c = getopt (argc, argv, "-dnbeo:t:v:m:p:r:")) != -1) {
		switch (c) {
		case '\1':
			if (!in[0]) 
				in[0]=optarg;
			else if (!in[1])		
				in[1]=optarg;
			else if (!in[2])		
				in[2]=optarg;
			else {
				usage(stderr); return 1;
			}
			++in_n;
			break;
                case 'o': if (out_n == 3) {
				usage(stderr); return 1;
			  }
			  out[out_n++] = optarg; 
			  break;
		case 'r': orep = optarg; break;
		case 't': threads = atoi(optarg); break;
		case 'm': mino = atoi(optarg); break;
		case 'p': pctdiff = atoi(optarg); break;
		case 'd': debug = 1; break;
                case 'v':
                        if (strlen(optarg)>1) {
                                fprintf(stderr, "Option -v requires a single character argument");
                                exit(1);
                        }
                        verify = *optarg; break;
		case '?': 
		     if (strchr("otvmpr", optopt))
		       fprintf (stderr, "Option -%c requires an argument.\n", optopt);
		     else if (isprint(optopt))
		       fprintf (stderr, "Unknown option `-%c'.\n", optopt);
		     else
		       fprintf (stderr,
				"Unknown option character `\\x%x'.\n",
				optopt);
		     usage(stderr);
             	     return 1;
		}
	}

	if (argc < 3 || !in[1] || (!in[2] && out_n != 1 && out_n != 3) || (in[2] && out_n != 1 && out_n != 5)) {
		usage(stderr);
		return 1;
	}

	FILE *fin[2];
	for (i = 0; i < in_n; ++i) {
		fin[i] = fopen(in[i], "r"); 
		if (!fin[i]) {
			fprintf(stderr, "Error opening file '%s': %s\n",in[i], strerror(errno));
			return 1;
		}
	}

	const char *suffix[5]={"un1", "un2", "join", "un3", "join2"};
        FILE *fout[5]; meminit(fout);
	char *pre = out[0];
        for (i = 0; i < (in[2] ? 5 : 3); ++i) {
		// prefix out
		if (out_n == 1) {
			out[i]=(char *)malloc(strlen(pre)+10);
			strcpy(out[i], pre);
			char *p;
			if (p=strchr(out[i], '%')) {
				// substiture instead of append
				strcpy(p, suffix[i]);
				strcpy(p+strlen(suffix[i]), pre+(p-out[i])+1);
			} else {
				strcat(out[i], suffix[i]);
			}
		} // else explicit
                fout[i] = fopen(out[i], "w");
                if (!fout[i]) {
                        fprintf(stderr, "Error opening output file '%s': %s\n",out[i], strerror(errno));
                        return 1;
                }
        }

//printf("in_n:%d in:%x fo:%x", in_n, in[3], fout[4]);
//return 1;

	FILE *frep = NULL;
	if (orep) {
                frep = fopen(orep, "w");
                if (!orep) {
                        fprintf(stderr, "Error opening report file '%s': %s\n",out[i], strerror(errno));
                        return 1;
                }
	}


	// some basic validation of the file formats
	{
		for (i=0;i<in_n;++i) {
			char c=getc(fin[i]);
			if (c != '@')  {
				fprintf(stderr, "%s doesn't appear to be a fastq file", in[i]);
				return 1;
			}
			ungetc(c, fin[i]);
		}
	}

	struct fq fq[3];	
        meminit(fq);

	int nrec=0;
	int nerr=0;
	int nok=0;
	int joincnt=0;
	double tlen=0;
	double tlensq=0;
	int read_ok;

	struct fq rc;
	meminit(rc);

	// read in 1 record from each file
	while (read_ok=read_fq(fin[0], nrec, &fq[0])) {
		for (i=1;i<in_n;++i) {
		int mate_ok=read_fq(fin[i], nrec, &fq[i]);
		if (read_ok != mate_ok) {
			fprintf(stderr, "# of rows in mate file '%s' doesn't match primary file, quitting!\n", in[i]);
			return 1;
		}
		if (verify) {
			// verify 1 in 100
			if (0 == (nrec % 100)) {
				char *p=strchr(fq[i].id.s,verify);
				if (!p) {
					fprintf(stderr, "File %s is missing id verification char %c at line %d", in[i], verify, nrec*4+1);
					return 1;
				}
				int l = p-fq[i].id.s;
				if (strncmp(fq[0].id.s, fq[i].id.s, l)) {
					fprintf(stderr, "File %s, id doesn't match file %s at line %d", in[0], in[i], nrec*4+1);
					return 1;
				}
			}
		}
		}

		++nrec;
		if (read_ok < 0) continue;

		if (debug) fprintf(stderr, "seq: %s %d\n", fq[0].seq.s, fq[0].seq.n);

		revcomp(&rc, &fq[1]);

		if (debug) fprintf(stderr, "comp: %s %d\n", rc.seq.s, rc.seq.n);

		int maxo = min(fq[0].seq.n, rc.seq.n);
		int bestscore=INT_MAX;
		int besto=-1;
		for (i=mino; i < maxo; ++i) {
			int mind = (pctdiff * i) / 100;
                        int d;
                        d=hd(fq[0].seq.s+fq[0].seq.n-i, rc.seq.s, i);
			if (d <= mind) {
				// squared-distance over length, probably can be proven better (like pearson's)
				// by someone who needs to write a paper
				int score = (1000*(d*d+1))/i;	
				if (score < bestscore) {
					bestscore=score;
					besto=i;
				}
			}
		}

		if (debug) {
			fprintf(stderr, "best: %d %d\n", besto, bestscore);
		}

		FILE *fmate = NULL;
		if (besto > 0) {
			++joincnt;
			tlen+=besto;
			tlensq+=besto*besto;
			int l=besto/2;			// discard from left
			int r=besto-(besto/2);			// discard from right
			FILE *f=fout[2];

			if (verify) {
				char *p=strchr(fq[0].id.s,verify);
				if (p) {
					*p++ = '\n';
					*p = '\0';
				}
			}
			fputs(fq[0].id.s,f);
			for (i = 0; i < besto; ++i ) {
				int li = fq[0].seq.n-besto+i;
				int ri = i;
				if (fq[0].seq.s[li] == rc.seq.s[ri]) {
					fq[0].qual.s[li] = max(fq[0].qual.s[li], rc.qual.s[ri]);
					rc.qual.s[ri] = max(fq[0].qual.s[li], rc.qual.s[ri]);
				} else {
					// use the better-quality read, although the qual should be downgraded due to the difference!
					if (fq[0].qual.s[li] > rc.qual.s[ri]) {
						rc.seq.s[ri] = fq[0].seq.s[li];
					} else {
						fq[0].seq.s[li] = rc.seq.s[ri];
					}
				}
			}
			fwrite(fq[0].seq.s,1,fq[0].seq.n-l,f);
			fputs(rc.seq.s+r,f);
			fputc('\n',f);
			fputs(fq[0].com.s,f);
			fwrite(fq[0].qual.s,1,fq[0].qual.n-l,f);
			fputs(rc.qual.s+r,f);
			fputc('\n',f);
			fmate=fout[4];

			if (frep) {
				fprintf(frep, "%d\n", besto);
			}
		} else {
			for (i=0;i<2;++i) {
				FILE *f=fout[i];
				fputs(fq[i].id.s,f);
				fputs(fq[i].seq.s,f);
				fputc('\n',f);
				fputs(fq[i].com.s,f);
				fputs(fq[i].qual.s,f);
				fputc('\n',f);
			}
			fmate=fout[3];
		}

		if (fmate) {
			fputs(fq[2].id.s,fmate);
			fputs(fq[2].seq.s,fmate);
			fputc('\n',fmate);
			fputs(fq[2].com.s,fmate);
			fputs(fq[2].qual.s,fmate);
			fputc('\n',fmate);
		}
	}


	double dev = sqrt((((double)joincnt)*tlensq-pow((double)tlen,2)) / ((double)joincnt*((double)joincnt-1)) );
	printf("Total reads: %d\n", nrec);
	printf("Total joined: %d\n", joincnt);
	printf("Average join len: %.2f\n", (double) tlen / (double) joincnt);
	printf("Stdev join len: %.2f\n", dev);

	return 0;
}

void usage(FILE *f) {
	fputs( 
"Usage: fastq-join [options] <read1.fq> <read2.fq> [mate.fq] -o <read.%.fq>\n"
"\n"
"Joins two paired-end reads on the overlapping ends.\n"
"\n"
"Options:\n"
"\n"
"-o FIL		See 'Output' below\n"
"-v C		Verifies that the 2 files probe id's match up to char C\n"
"		  use '/' for Illumina reads\n"
"-p N		N-percent maximum difference (.20)\n"
"-m N		N-minimum overlap (6)\n"
"-r FIL		Verbose stitch length report\n"
"\n"
"Output: \n"
"\n"
"  You can supply 3 -o arguments, for un1, un2, join files, or one \n"
"argument as a file name template.  The suffix 'un1, un2, or join' is \n"
"appended to the file, or they replace a %-character if present.\n"
"\n"
"  If a 'mate' input file is present (barcode read), then the files\n"
"'un3' and 'join2' are also created.\n"
"\n"
	,f);
}
