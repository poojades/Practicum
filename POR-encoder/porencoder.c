#include "keygenwrapper.h"
#include "eccwrapper.h"
#include "FeistelPRP.h"
#include "encwrapper.h"
#include "jg_timing.h"

#define v 1024/32
#define w 4096/32
#define n 255
#define k 223
#define n1 64
#define k1 32
#define n2 64
#define k2 32

static unsigned long q,t;
extern unsigned char k_file_perm[16],k_ecc_perm[16],k_ecc_enc[16],
	k_chal[16],k_ind[16],k_enc[16],k_mac[16];
double totalTime,totalStartTime, totalEndTime;
double readTime,readStartTime, readEndTime;
double prpTime,prpStartTime, prpEndTime;
double eccTime,eccStartTime, eccEndTime;
double macTime,macStartTime, macEndTime;
double chalTime,chalStartTime, chalEndTime;
double write1Time,write1StartTime, write1EndTime;
double write2Time,write2StartTime, write2EndTime;
double encTime,encStartTime,encEndTime;
clock_t startTime,endTime;
struct timespec start, finish;

typedef struct {
	unsigned long s[v];
	unsigned int u;
} Chal;

int hmac(char* filename,unsigned char* dst,unsigned char* key)
{
	int idx, err;
	hmac_state hmac;
	unsigned long dstlen;
	if (register_hash(&sha1_desc)==-1) {
		printf("error registering SHA1\n");
		return -1;
	}
	idx = find_hash("sha1");

	dstlen = 16;

	if ((err = hmac_file(idx,filename,key,16,dst,&dstlen))!=CRYPT_OK) {
		printf("Error hmac: %s\n",error_to_string(err));
		return -1;
	}
    
    printf("hmac complete\n");
    return 0;
}

void displayCharArray(unsigned char* out,int len)
{
	int i;
	for (i = 0;i < len; i++) {
		printf("%02x", out[i]);
	}
	printf("\n");
}

int blockize(FILE* fp)
{
	unsigned long fileLen;
	unsigned int file_blocks;
	unsigned int i;
	fseek(fp,0,SEEK_END);
	fileLen = ftell(fp);
	printf("\nfile size: %lu\n",fileLen);
	if(fileLen % 32==0) {
		file_blocks = fileLen/32;
		printf("There are %d 32-byte blocks\n",file_blocks);
	}
	else
	{
		file_blocks = fileLen/32+1;
		int padding = 32 - fileLen % 32;
		unsigned char paddingBytes[padding];
		for (i=0;i<padding;i++)
			paddingBytes[i] = 0;
        write1StartTime = getCPUTime();
		fwrite(paddingBytes,padding,1,fp);
        write1Time += getCPUTime() - write1StartTime;
		printf("After padding %d zeros, there are %d 32-byte blocks\n",padding,file_blocks);
	}
	return file_blocks;
}

int inc_encoding (FILE* fp,int* prptable)
{
	printf("\nIncremental encoding starts...\n");
	int i,j,enc_blocks,d=n-k;
	fseek(fp,0,SEEK_END);
	fileLen = ftell(fp);
	if(fileLen % k==0) 
		enc_blocks = fileLen/k;
	else
		enc_blocks = fileLen/k+1;
	printf("There are %d encoding blocks\n",enc_blocks);
	unsigned char message[k];
	unsigned char codeword[n];
	unsigned char ** code;//[enc_blocks][d];
	int readLen = 512*1024*1024;

	long filecounter = 0;
	int blockcounter = 0;
	int round = 0;

	code = (unsigned char **) malloc(enc_blocks*sizeof(unsigned char *));  
	for (i = 0; i < enc_blocks; i++) {
   	code[i] = (unsigned char *) malloc(d*sizeof(unsigned char));  
   	}
   	rewind(fp);
	while (!feof(fp))
	{
		unsigned char * buf; 
		if ((buf = malloc(sizeof(unsigned char)*readLen))==NULL) {
			printf("malloc error: inc_encoding\n");
			exit(1);
		}
		readStartTime = getCPUTime();
		clock_gettime(CLOCK_MONOTONIC, &start);
		printf("max read in %d bytes\n",readLen);
		size_t br = fread(buf, 1, readLen, fp);
		printf("Read in %lu bytes\n",br);
		fflush(stdout);
		clock_gettime(CLOCK_MONOTONIC, &finish);
		double addTime = finish.tv_sec - start.tv_sec;
		addTime += (finish.tv_nsec - start.tv_nsec)/1000000000.0;
		readTime += getCPUTime() - readStartTime+addTime;
		filecounter = filecounter + br;
		if (br!=0) {
			printf("round %d\n",round);
			printf("filecounter = %lu\n",filecounter);
			for(i=0;i<enc_blocks;i++) {
				for(j=0;j<k;j++) {
					int index = i*k+j;
					int block_index = index/32;
					int byte_index = index%32;
					if (index>=fileLen) {
						int a;
						for(a=j;a<k;a++)
							message[a]=0;
						break;
					}
					//printf("block_index=%d, prpind=%d, byteind=%d, ",block_index,prptable[block_index],byte_index);
					unsigned long file_index = prptable[block_index]*32+byte_index;
					//printf("file_index=%lu, ",file_index);
					if(file_index<filecounter && file_index>=(filecounter-br)) {
						unsigned long newind = file_index-filecounter+br;
						//printf("newind=%lu\n ",newind);
						message[j] = buf[newind];
					}
					else 
						message[j] = 0;
				}
				//printf("msg for block %d: ",i);
				//displayCharArray(message,k);
				encode_data(message,k,codeword);
				for(j=0;j<d;j++)
					code[i][j] = code[i][j] ^ codeword[k+j];
			}
			round = round + 1;
		}
		free(buf);
	}
	/*// ------------- bug checking
	unsigned char a[fileLen],r[fileLen];
	unsigned char newc[n],newm[k];
	rewind(fp);
	fread(a, 1, fileLen, fp);
	printf("original:\n");
	for (i=0;i<fileLen;i++) {
		printf("%02x",a[i]);
	}
	printf("\n");
	for (i=0;i<fileLen/32;i++) {
		for (j=0;j<32;j++) {
			r[i*32+j] = a[prptable[i]*32+j];
		}
	}
	printf("prped:\n");
	for (i=0;i<fileLen;i++) {
		printf("%02x",r[i]);
	}
	printf("\n");
	for (i=0;i<enc_blocks;i++) {
		printf("parity part %d: ",i);
		displayCharArray(code[i],d);

		unsigned char newcode[n];
		int iii;
		int ii;
		for(ii=0;ii<k;ii++) {
			if (i*k+ii>=fileLen)
				break;
			newcode[ii] = r[i*k+ii];
			newm[ii] = r[i*k+ii];
		}
		if (i==enc_blocks-1) {
			for(ii=0;ii<k-fileLen%k;ii++){
				newm[fileLen%k+ii]=0;
				newcode[fileLen%k+ii] = 0;
			}
		}
		encode_data(newm,k,newc);
		printf("actual code %d: ",i);
		displayCharArray(newc,n);
		for(iii=0;iii<d;iii++) {
			newcode[k+iii] = code[i][iii];
		}
		newcode[0] = 99;
		printf("whole code %d: ",i);
		displayCharArray(newcode,n);
		decode_data(newcode, n);
		int erasure[1];
		int syn = check_syndrome ();
		printf("syndrome: %d\n",syn);
		if (syn != 0) {
			correct_errors_erasures(newcode,n,0,erasure);
		}
		printf("decode %d: ",i);
		displayCharArray(newcode,n);
	}
	//--------------- bug checking*/
	free(prptable);
	prptable = NULL;
	//free(buf);
	prptable = malloc(sizeof(int)*(enc_blocks));
	printf("\nSRF PRP for the outer layer ECC...\n");
   prpStartTime = getCPUTime();
	prptable = prp(enc_blocks, k_ecc_perm);
   prpTime += getCPUTime() - prpStartTime;
	enc_init(k_ecc_enc);
	//printf("check here...%lu\n",sizeof(prptable));
	for (i=0;i<enc_blocks;i++) {
		//initialize_ecc();
		unsigned char ct[d];
    	//printf("%d,",prptable[i]);
    	encStartTime = getCPUTime();
		encrypt(ct,code[prptable[i]],sizeof(ct));
		encTime += getCPUTime()-encStartTime;
		//printf("encrypted for %d: ",i);
		//displayCharArray(ct,sizeof(ct));
		//unsigned char pt[d];
		//decrypt(ct,pt,sizeof(ct));
		//printf("decrypted for %d: ",i);
		//displayCharArray(pt,sizeof(ct));
		write1StartTime = getCPUTime();
		fwrite(ct,d,1,fp);
    	write1Time += getCPUTime()-write1StartTime;
	}
	//printf("check here...\n");
	t = t+enc_blocks;
	printf("\nIncremental encoding finishes...\n");
	free(prptable);
	for (i = 0; i < enc_blocks; i++){  
   	free(code[i]);  
	}  
	free(code); 
	return 0;
}

void concat_encode(unsigned char * message,unsigned char* codeword) {
	unsigned char tmp_code[v*32*n1/k1],stripe[k1],stripe_code[n1];
	int index,i,j;
	
	for (index=0;index<v*32;index++) {
		tmp_code[index] = message[index];
	}
	for (i=0;i<v;i++) {
		for (j=0;j<sizeof(stripe);j++) {
			stripe[j] = message[i*k1+j];
		}
		initialize_ecc();
		encode_data(stripe,k1,stripe_code);
		for (j=0;j<n1-k1;j++) {
			tmp_code[index] = stripe_code[k1+j];
			index++;
		}
	}
	index = 0;
	for (i=0;i<v*n1/k1;i++) {
		for (j=0;j<sizeof(stripe);j++) {
			stripe[j] = tmp_code[i*k2+j];
		}
		encode_data(stripe,k2,stripe_code);
		for (j=0;j<n2;j++) {
			codeword[index] = stripe_code[j];
			index++;
		}
	}
}

int precompute_response(FILE* fp, Chal * c,char * key) {
    //chalStartTime = getCPUTime();
	unsigned char message[v*32];
	unsigned char codeword[w*32];
	char uth[32];
	char ct[32];
	int i,j,p;
	enc_init(key);
	for (j=0;j<q;j++) {
	//printf("Precomputation for challenges No.%d\n",j);
		int index = 0;
		for (i=0;i<v;i++) {
			//printf("s[%d]=%lu\n",i,c[j].s[i]);
			fseek(fp,c[j].s[i]*32,SEEK_SET);
			unsigned char buffer[32];
          readStartTime = getCPUTime();
		clock_gettime(CLOCK_MONOTONIC, &start);
			fread(buffer, 32, 1, fp);
		clock_gettime(CLOCK_MONOTONIC, &finish);
		double addTime = finish.tv_sec - start.tv_sec;
		addTime += (finish.tv_nsec - start.tv_nsec)/1000000000.0;
         readTime += getCPUTime() - readStartTime+addTime;
			for(p=0;p<32;p++) {
				message[index] = buffer[p];
				index++;
			}
			fflush(stdout);
		}

		concat_encode(message,codeword);
		for (i=0;i<32;i++) {
			uth[i] = codeword[32*c[j].u+i];
		}
		//printf("u=%d\n",c[j].u);
        //chalTime += getCPUTime() - chalStartTime;

		encStartTime = getCPUTime();
		encrypt(ct,uth,sizeof(uth));
		encTime += getCPUTime()-encStartTime;
		//printf("Precomputation for response No.%d\n",j);
        write2StartTime = getCPUTime();
		clock_gettime(CLOCK_MONOTONIC, &start);
		fwrite(ct,32,1,fp);
		clock_gettime(CLOCK_MONOTONIC, &finish);
		double addTime = finish.tv_sec - start.tv_sec;
		addTime += (finish.tv_nsec - start.tv_nsec)/1000000000.0;
        write2Time+=getCPUTime()-write2StartTime+addTime;
		//fflush(stdout);
	}
}

int main(int argc, char* argv[])
{
    totalStartTime = getCPUTime();
    printf("%lf\n",totalStartTime);
	int i;
	FILE* fp = fopen(argv[1],"a+b");
	if (fp==NULL) {
		printf("fopen error: cannot open file\n");
		exit(1);
	}
	int* prptable;
	unsigned char mac[MAXBLOCKSIZE];
    unsigned long fileLen1,fileLen2;
	fseek(fp,0,SEEK_END);
	fileLen1 = ftell(fp);
	//k_file_perm[16],k_ecc_perm[16],k_ecc_enc[16],
	//k_chal[16],k_ind[16],k_enc[16],k_mac[16];
	
	master_keygen(argv[2]);
	//keygen(k_file_perm,16);
	printf("key for file permutation: ");
	displayCharArray(k_file_perm,16);
	//keygen(k_ecc_perm,16);
	printf("key for ecc permutation: ");
	displayCharArray(k_ecc_perm,16);
	//keygen(k_ecc_enc,16);

	printf("key for ecc encryption: ");
	displayCharArray(k_ecc_enc,16);
	//keygen(k_chal,16);
	printf("key for challenge generation: ");
	displayCharArray(k_chal,16);
	//keygen(k_ind,16);
	printf("key for random index generation: ");
	displayCharArray(k_ind,16);
	//keygen(k_enc,16);
	printf("key for response encryption: ");
	displayCharArray(k_enc,16);
	//keygen(k_mac,16);
	printf("key for MAC computation: ");
	displayCharArray(k_mac,16);	
	
	int blocks = blockize(fp);
	t = blocks;
	fclose(fp);
    fp = fopen(argv[1],"a+b");
	printf("\nComputing file's MAC...\n");
		printf("\nmac size %lu\n",sizeof(mac));
    macStartTime = getCPUTime();
	hmac(argv[1],mac,k_mac);
    macTime = getCPUTime() - macStartTime;
	printf("\nMAC = ");
	displayCharArray(mac,16);	
	
	prptable = malloc(sizeof(int)*blocks);
	printf("\nSRF PRP for the entire file...\n");
    prpStartTime = getCPUTime();
	prptable = prp(blocks, k_file_perm);
    prpTime += getCPUTime() - prpStartTime;
	//for(i=0;i<blocks;i++)
	//	printf("%d -> %d\n",i,prptable[i]);
    eccStartTime = getCPUTime();
	initialize_ecc();
	inc_encoding(fp,prptable);
    eccTime = getCPUTime() - eccStartTime - readTime;
	
	printf("\nFile blocks after outer layer encoding: %lu\n",t);

	q = 100;
	printf("\nPrecomputation for %lu challenges and responses\n",q);
    chalStartTime = getCPUTime();
	Chal c[q];
	int j,p;
	keygen_init();
	seeding(k_chal);
	unsigned char * kjc[q];
	for(j=0;j<q;j++) {
		kjc[j] = malloc(16*sizeof(unsigned char *));
		keygen(kjc[j], 16);
		//printf("display kjc for j=%d\n",j);
		//displayCharArray(kjc[j],16);
	}
	for(j=0;j<q;j++) {
		keygen_init();
		seeding(kjc[j]);
		for(p=0;p<v;p++) {
			unsigned long randomIndex;
			char rand[8];
			keygen(rand, 8);
			//printf("display rand for j=%d,v=%d\n",j,p);
			randomIndex = *(unsigned long *)rand;	
			c[j].s[p] = randomIndex % t;
			//printf("display random index for j=%d,v=%d: %lu\n",j,p,c[j].s[p]);
		}
	}
	keygen_init();
	seeding(k_ind);	
	for(j=0;j<q;j++) {
		unsigned int randomIndex;
		char rand[4];
		keygen(rand, 4);
		randomIndex = *(unsigned int *)rand;	
		c[j].u = randomIndex % w;
		//printf("display rand for j=%d,u=%d\n",j,c[j].u);
	}
	printf("Precomputation for challenges finishes\n");

    //printf("%lf\n",chalTime);
	precompute_response(fp,c,k_enc);
	printf("Precomputation for responses finishes\n");
	    chalTime+=getCPUTime()-chalStartTime - write2Time;
	printf("\nAppend MAC to the end of the file...\n");
    write2StartTime = getCPUTime();
		clock_gettime(CLOCK_MONOTONIC, &start);
	fwrite(mac,16,1,fp);
		clock_gettime(CLOCK_MONOTONIC, &finish);
		double addTime = finish.tv_sec - start.tv_sec;
		addTime += (finish.tv_nsec - start.tv_nsec)/1000000000.0;
    write2Time += getCPUTime() - write2StartTime+addTime;
    fseek(fp,0,SEEK_END);
	fileLen2 = ftell(fp);
	fclose(fp);
	//free(kjc);
	printf("\nPOR encoding done\n");
	//printf("%lf\n",getCPUTime());
    totalTime = getCPUTime() - totalStartTime;
    printf("#RESULT#\n");
    printf("%lu\n",fileLen1);
    printf("%lu\n",fileLen2);
    printf("%lf\n",totalTime);
    printf("%lf\n",readTime);
    printf("%lf\n",prpTime);
    printf("%lf\n",eccTime);
    printf("%lf\n",macTime);
    printf("%lf\n",chalTime);
    printf("%lf\n",write1Time+write2Time);
    printf("%lf\n",encTime);


}



