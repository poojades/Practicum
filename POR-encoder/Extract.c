#include <stdio.h>
#include <stdlib.h>
#include "eccwrapper.h"
#include "FeistelPRP.h"
#include "encwrapper.h"
#include "keygenwrapper.h"
#include <math.h>

#define alpha 10
#define delta 0.25
#define BLOCK_SIZE 32
#define n 255
#define k 233
#define d 32
#define w 4096/32 
#define v 1024/32 
#define MACSIZE 16
#define n1 64
#define k1 32
#define d1 32
#define n2 64
#define k2 32
#define d2 32

static unsigned long q,t;
static unsigned	char uth[32];

//Challenge structure
typedef struct { 
	unsigned long j;
	unsigned char k_j_c[16];
	int u;
}chal;


//decoding file blocks structure
typedef struct d_b{
	unsigned char file_blocks[alpha][32]; 
	unsigned char frequency[alpha]; 
}decoded_blocks; 

extern unsigned char k_file_perm[16],k_ecc_perm[16],k_ecc_enc[16],
	k_chal[16],k_ind[16],k_enc[16],k_mac[16];

int outer_decoding(FILE* temp_fp, FILE *output, decoded_blocks *db);
void inner_decoding(decoded_blocks *db,unsigned char * c_in_codeword, unsigned long * indices);
void inner_GMD(decoded_blocks *db,unsigned char * c_in_codeword, unsigned long * indices,FILE * fp);

void displayCharArray(unsigned char* out,int len)
{
	int i;
	for (i = 0;i < len; i++) {
		printf("%02x", out[i]);
	}
	printf("\n");
}

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
    
	dstlen = sizeof(dst);
    
	if ((err = hmac_file(idx,filename,key,16,dst,&dstlen))!=CRYPT_OK) {
		printf("Error hmac: %s\n",error_to_string(err));
		return -1;
	}
    
    printf("hmac complete\n");
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

unsigned char * execute_challenge(FILE* fp, int j, char * kjc, int u, unsigned long * indices) {
	unsigned char message[v*32];
	unsigned char codeword[w*32];

	int pos = 0;
	int i,p;
	keygen_init();
	seeding(kjc);
	for(p=0;p<v;p++) {
		unsigned long randomIndex;
		char rand[8];
		keygen(rand, 8);
		indices[p] = *(unsigned long *)rand%t;
	}
	int index = 0;
	for (i=0;i<v;i++) {
		fseek(fp,indices[i]*32,SEEK_SET);
		unsigned char buffer[32];
		fread(buffer, 32, 1, fp);
		for(p=0;p<32;p++) {
			message[index] = buffer[p];
			index++;
		}
	}
	concat_encode(message,codeword);
	for (i=0;i<32;i++) {
		uth[i] = codeword[32*u+i];
	}
	return uth;
}

int main(int argc,char** argv)
{
	printf("start POR extract...\n");
	char * filename = argv[1];
	int p;
	t = atoi(argv[3]);
	printf("extract for file \"%s\" with %lu blocks\n",filename,t);
	int tempv = v;
	q = alpha * t / tempv;
	decoded_blocks * db;
	unsigned int i,j,u,size,index;
	char * codeword,mac;
	chal c[q];
	unsigned char k_j_c[16]; 
	char str[999]; 
	FILE *fp1,*fp2,*temp_fp; 
	char * temp_block;
	// after writing new file, delete old file

	char * r_file="recovered";
	char * temp ="temp";
	unsigned int * v_chal_indices;

	//use master key and call keygen to generate all the keys here.	
	master_keygen(argv[2]); // integrate
	
	//open encoded file for reading	
  	if ((fp1 = fopen(filename, "r")) == NULL){
	         printf("couldn't open input file for reading.\n");
	         return -1;
    }

	//read mac from the end of the old file	
	unsigned char originalmac[16];
	int bufLength=16;
	fseek(fp1, 0, SEEK_END); // read file before
	long fileLength=ftell(fp1);
	fseek(fp1, fileLength-bufLength, SEEK_SET);
	fread(originalmac, sizeof(originalmac), 1, fp1);
	printf("\nMAC attached at the end of the file: ");
	displayCharArray(originalmac,16);
	//open output file for writing
	if ((fp2 = fopen(r_file, "w+")) == NULL){
	         printf("couldn't open output file for writing.\n");
	         return -1;
    }

	//open temp file for writing
	if ((temp_fp = fopen(temp, "w+")) == NULL){
	         printf("couldn't open temperory file for writing.\n");
	         return -1;
    }

	//allocate memory for d1
	db = malloc (sizeof(struct d_b)*t);  
	//for(i=0;i<t;i++) {
	//	printf("for db[%d], sizeof frequency=%lu\n",i,sizeof(db[i].frequency));
	//	printf("display frequency[5] %d\n",db[i].frequency[5]);
	//}
	if(db == NULL) {
		printf("failed to allocate memory for d.\n");
		return -1;
	}

	//total number of challenges		
	size = alpha*(t/v); 

	//allocate memory for the challenge set
	//if ((c = (chal *)malloc(sizeof(chal)*size))== NULL) {
	//	fprintf(stderr, "failed to allocate memory for challenges.\n");
	//	return -1;
	//}
	
	// populate challenge set
	printf("\nstart inner layer decoding...\n");
	printf("generate %lu challenges\n",q);
	keygen_init();
	seeding(k_chal);	
	for (j=0;j<q;j++){
		keygen(c[j].k_j_c, 16);
		c[j].j = j;
	}
	printf("kjc for each challenge generated\n");
	
    // for each challenge
	for (i=0;i<q;i++){
		
		//unsigned char * codeword = (unsigned char *) malloc(sizeof(unsigned char)*32*w);
		unsigned char codeword[32*w];
		unsigned long indices[v];

		index = 0;
		//execute each challenge w times
		for(u=0;u<w;u++){
			unsigned char * subcode = execute_challenge(fp1,c[i].j, c[i].k_j_c, u, indices);
			//printf("%d-th sub code\n",u);
			//displayCharArray(subcode,32);
			int tempI;
			for(tempI=0;tempI<32;tempI++)
				codeword[index++] = subcode[tempI];
		}
		//printf("codeword for challenge #%d\n",i);
		//displayCharArray(codeword,4096);
		// inner code decoding
		printf("start decoding for challenge #%d\n",i);
		inner_GMD(db,codeword,indices,fp1); 
		printf("finish decoding for challenge %d\n",i);

		//free the memory
		//free(codeword);
		//free(indices);

		//delete old file
			
		//remove(filename);
	}
	
	for (i=0;i<t;i++){
		int max_frequency=0;
		int max_index=0;
		
		for(j=0;j<sizeof(db[i].frequency);j++){
			if(db[i].frequency[j] > max_frequency){
				max_frequency = db[i].frequency[j];
				max_index = j;
			}
		}
        if(max_frequency==0) {
            fseek(fp1,i*32,SEEK_SET);
            unsigned char buffer[32];
            fread(buffer, 32, 1, fp1);
            fwrite(buffer,32,1,temp_fp);
        }
		else {
		//check if the location can be corrected or has erasure 
		if(ceil(max_frequency / sizeof(db[i].frequency)) > (delta+0.5)){
			fwrite(db[i].file_blocks[max_index],32,1,temp_fp);
		}else{
			fwrite(db[i].file_blocks[max_index],32,1,temp_fp);
			//where to get the index from
			//db[i].file_blocks[0]=NULL;
	
			//-1 indicating erasure
			db[i].frequency[0]=-1;
		}
        }
	}
    fclose(fp1);
	fclose(temp_fp);
	
	//perform outer decoding
	//outer_decoding(temp_fp,fp2,db);
	
	//compute mac
	unsigned char newmac[16]; 
	//hmac("temp",newmac,k_mac);
    //printf("display MAC\n");
    //displayCharArray(newmac,16);
	
	//if verified, print the file. Else output the error
	int flag=1;	
	for(i=0;i<MACSIZE;i++){
		if(newmac[i]!=originalmac[i]){	
			flag=0;
			break;
		}
	}
	if (flag==1){
		printf("Your file is recovered\n");
    		while (fscanf(fp2, "%s", str)!=EOF){
        		printf("%s",str);
		}
	}else{
		//printf("Your file can not be recovered.\n");
		return -1;
	}

	fclose(fp2);
	return 0;
}

// Procecdure for OUTER DECODING
/* 1. decrypt the parity block using key k3
2. decode using key k2
3. unpermute all parity blocks and store
4. for the permutted message, get each of the strip , append it with parity. 
5. deocode using ecc out
6. we will get permutted file
7. unpermute it using pRP table 
*/

/*Input parameters : 
1. original file pointer
2. int array for erasure locations
3. recovered data from challenges
4. prp table
5. keys for permutation and encryption*/

void reverseprp(int stripes, int * prp_table, int * reverse_prp_table)
{
	int i=0;
	for(i=0;i<sizeof(prp_table);i++){
		int num=prp_table[i];
		reverse_prp_table[num]=i;	
	}
}

int outer_decoding(FILE* temp_fp, FILE *output, decoded_blocks *db)
{
	int i,j,stripes,fileLen,index,m;
	int * prp_table, *reverse_prp_table;
	char message[k];
	char codeword[n];
	char parity[stripes][d]; 
	char decodedfile[stripes][k];
	int erasure[1];

	//find the number of stripes in the file 
	fseek(temp_fp,0,SEEK_END);
	fileLen = ftell(temp_fp);
	if(fileLen % k==0) 
		stripes = fileLen/k; 
	else
		stripes = fileLen/k+1;	

	//call prp
	prp_table =(int *)malloc(sizeof(int)*stripes);
	reverse_prp_table = (int *)malloc(sizeof(int)*stripes);
	
	//perform reverse prp
	prp_table = prp(stripes, k_ecc_perm);
	reverseprp(stripes,prp_table,reverse_prp_table);
	free(prp_table);

	enc_init(k_ecc_enc);
	
	//decrypt parity part
	rewind(temp_fp);
	
	///number of parity blocks = stripes/2
	//read parity parts directly
	// not sure about block length : CHECK WITH JIE,Rucha
	//can we do directly like this?
	int parity_start = fileLen-(stripes/2)*d;
	fseek(temp_fp,parity_start,SEEK_SET);

	for (i=0;i<stripes/2;i++) {
		unsigned char ct[d];
		unsigned char pt[d];	
		
		fread(pt,sizeof(pt),1,temp_fp);
		
		decrypt(ct,pt,sizeof(ct)); 
		for(j=0;j<d;j++){
			parity[i][j]=ct[j];
		}
	}

	//get the message and the parity, create codeword and decode it
	rewind(temp_fp);
	
	for(i=0;i<stripes/2;i++) {
		index=0;
		unsigned char pt[k];

		fread(pt,sizeof(pt),1,temp_fp);

		for(j=0;j<k;j++) {
			codeword[index++] = pt[j]; 
		}
		//calculate block index of the parity part
		int parity_index = (reverse_prp_table[i]*32);
		
		for(j=0;j<d;j++){
			codeword[index++]=parity[parity_index][j];
		}
		
		correct_errors_erasures(codeword,n,0,erasure); 
	
		for(m=0;m<k;m++){
			decodedfile[i*k][m]=codeword[m];
		}
	}
	
	// write data to the file by applying second level of permutation
	for(i=0;i<stripes;i++){
		int fileindex=reverse_prp_table[i];
		int block_start_index= i*32;
		int block_end_index=(i+1)*32;
		unsigned char ct[32];
		for(j=i*block_start_index;j<block_end_index;j++){
			//ct[j]=decodedfile[j];
		}
		fwrite(ct,sizeof(ct),1,output);
	}
	fclose(output);
	//delete(temp_fp);
	return 0;
}

//concatenated decoding
void inner_decoding(decoded_blocks *db,unsigned char * c_in_codeword, unsigned long * indices){ 
	int p;
		//for(p=0;p<v;p++) {
		//	printf("random index #%d: %lu\n",p,indices[p]);
		//}
	unsigned char c_in_message[v*k2],temp[n2],c_out_codeword[n1],c_out_message[v*k1];
	int c_index=0,m_index=0,i,j,m,index=0;
	int erasure[1];
	
	// cin decoding
	printf("concatenated Cin decoding...\n");
	for(j=0;j<w*32/n2;j++){
		for(i=0;i<n2;i++){
			temp[i]=c_in_codeword[c_index++];
		}
		decode_data(temp, n2);
		if (check_syndrome () != 0) {
			correct_errors_erasures(temp,n2,0,erasure);
		}
		for(i=0;i<k2;i++){
			c_in_message[m_index++]=temp[k2];	
		}
	}

	//cout decoding: get the file and parity part
	printf("concatenated Cout decoding...\n");
	c_index=0;
	for(i=0;i<v;i++){
		index=0;
		//create codeword
		//copy message part
		for(j=0;j<k1;j++){
			c_out_codeword[index++]=c_in_message[j];		
		}

		//copy parity part codeword
		p = (m_index/2) + (i*d1);
		for(j=0;j<d1;j++){
			c_out_codeword[index++]=c_in_message[j+p];		
		}	
		decode_data(c_out_codeword, n1);
		if (check_syndrome () != 0) {
			correct_errors_erasures(c_out_codeword,n1,0,erasure);
		}
		
		for(j=0;j<k1;j++){
			c_out_message[c_index++]=c_out_codeword[j];
		}
	}
	
	//c_out_message contains v decoded blocks
	//divide decoded message into v blocks from f1 to fv. 
	printf("updating Di...\n");
	for(i=0;i<v;i++){
		printf("indices[%d]=%lu\n",i,indices[i]);
		//divide codeword into 32 byte blocks
		int fi = indices[i];
		char block[32];
		for(j=0;j<32;j++){
			block[j]=c_out_message[(i*32)+j];
		}

		//check if similar codeword was already decoded and present in Di
		//if yes, just increase the frequency
		int notfound=1;
		for(j=0;j<alpha;j++){
			int flag=1;
			fflush(stdout);
			if(db[fi].frequency[j]==0) break;
			for(m=0;m<32;m++){
				if(db[fi].file_blocks[j][m]!=block[m]){
					flag=0;
					break;
				}		
			}
			if(flag){
				//int freq = db[fi].frequency[j];
				db[fi].frequency[j]++;	
				notfound=0;		
			}		
		}

		//if not found, add it in di		
		if(notfound){
			for(m=0;m<32;m++){
				db[fi].file_blocks[j][m] = block[m];		
			}
			db[fi].frequency[j] = 1;
		}	
	}
}

void inner_GMD(decoded_blocks *db,unsigned char * c_in_codeword, unsigned long * indices, FILE * fp){ 	
	unsigned char c_in_message[n1*k2],temp[n2],c_out_codeword[n1],c_out_message[v*32];
	int c_index=0,m_index=0,i,j,m,index=0;
	int erasure_index[n1];
	
	// cin decoding
	printf("concatenated Cin decoding...\n");
	for(j=0;j<n1;j++){
		for(i=0;i<n2;i++){
			temp[i]=c_in_codeword[c_index++];
		}
		unsigned char cpytemp[n2];
		memcpy(cpytemp,temp,n2);
		decode_data(temp, n2);
		if (check_syndrome () != 0) {
			int erasure[1];
			correct_errors_erasures(temp,n2,0,erasure);
		}
		int delta_dist = 0;
		for(i=0;i<n2;i++){
			if(temp[i]!=cpytemp[i])
				delta_dist++;
		}
		double prob;
		if(delta_dist<d2/2)
			prob = 2*(double)delta_dist/d2;
		else
			prob = 1.0;
		srand(time(NULL));
		double random_num = (double)rand() / (double)RAND_MAX;
		for(i=0;i<k2;i++){
			if (random_num<prob)
				c_in_message[m_index++]=0;
			else
				c_in_message[m_index++]=temp[i];	
		}
		if (random_num<prob)
			erasure_index[n1] = 1;
		else
			erasure_index[n1] = 0;
	}
	
	//printf("display c_in_message\n");
	//displayCharArray(c_in_message,sizeof(c_in_message));
	printf("concatenated Cout decoding...\n");
	c_index=0;
	for(i=0;i<v;i++){
		int erasure[n1];
		int num_erasure = 0;
		index=0;
		//create codeword
		//copy message part
		for(j=0;j<k1;j++){
			c_out_codeword[index++]=c_in_message[i*k1+j];		
		}
		int p;
		//copy parity part codeword
		p = v*32 + (i*d1);
		for(j=0;j<d1;j++){
			c_out_codeword[index++]=c_in_message[j+p];		
		}	
		//printf("display c_out_codeword for %d\n",i);
		//displayCharArray(c_out_codeword,sizeof(c_out_codeword));
		
		if(erasure_index[v]==1) {
			int ki;
			for(ki=0;ki<k2;ki++) {
				erasure[num_erasure] = ki;
				num_erasure++;
			}
		}
		if(erasure_index[v]==1) {
			int di;
			for(di=0;di<d2;di++) {
				erasure[num_erasure] = k2+di;
				num_erasure++;
			}
		}		
		decode_data(c_out_codeword, n1);
		if (check_syndrome () != 0) {
			correct_errors_erasures(c_out_codeword,n1,num_erasure,erasure);
		}
		
		for(j=0;j<k1;j++){
			c_out_message[c_index++]=c_out_codeword[j];
		}
	}
	
	//printf("updating Di...\n");
	for(i=0;i<v;i++){
		//printf("indices[%d]=%lu\n",i,indices[i]);
		//divide codeword into 32 byte blocks
		int fi = indices[i];
		unsigned char block[32];
		for(j=0;j<32;j++){
			block[j]=c_out_message[(i*32)+j];
		}

		//check if similar codeword was already decoded and present in Di
		//if yes, just increase the frequency
		int notfound=1;
		for(j=0;j<alpha;j++){
			int flag=1;
			if(db[fi].frequency[j]==0) break;
			for(m=0;m<32;m++){
				if(db[fi].file_blocks[j][m]!=block[m]){
					flag=0;
					break;
				}		
			}
			if(flag){
				//int freq = db[fi].frequency[j];
				db[fi].frequency[j]++;	
				notfound=0;		
			}		
		}

		//if not found, add it in di		
		if(notfound){
			for(m=0;m<32;m++){
				db[fi].file_blocks[j][m] = block[m];		
			}
			db[fi].frequency[j] = 1;
		}
        /*
		printf("display db %d:\n",fi);
		displayCharArray(db[fi].file_blocks[j],32);
		fseek(fp,fi*32,SEEK_SET);
		unsigned char buffer[32];
		fread(buffer, 32, 1, fp);
		printf("real content in the file block %d:\n",fi);
		displayCharArray(buffer,32);
         */
	}
}