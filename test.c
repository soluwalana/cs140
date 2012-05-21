#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

int dir_path_and_leaf (char *full, char **path, char **leaf){
  uint32_t last_slash_pos = 0;
  uint32_t char_count = 0;
  
  //printf("full %s\n", full);
  while(full[char_count] != '\0'){
    if(full[char_count] == '/'){
      last_slash_pos = char_count;
    }
    char_count ++;
  }
  //printf("Pos of last slash was %u\n", last_slash_pos);
  int is_relative;
  if(*full == '/'){
    //printf("root\n");
    if(last_slash_pos == 0){
      if(char_count == 1){
	/* Leaf is root directory*/
	*leaf = (full);
      }else{
	/*leaf in root directory*/
	*leaf = (full + 1);
      }
      *path = NULL;
    }else{
      /* leaf at end of string */
      *path = full;
      full[last_slash_pos] = '\0';
      *leaf = (full + last_slash_pos + 1);
    }
    is_relative = 0;
  }else{
    //printf("relative\n");
    if(last_slash_pos == 0){
      /*leaf is the only thing handed in*/
      *leaf = (full);
      *path = NULL;
      //printf("1\n");
    }else{
      *path = full;
      full[last_slash_pos] = '\0';
      *leaf = (full + last_slash_pos + 1);
      //printf("2");
    }
    is_relative = 1;
  }

  if(**leaf == '\0'){
    /* No Leaf though :(*/
    *leaf = NULL;
  }

  return is_relative;
}

/* Returns the length of STRING. */
size_t strlen1 (const char *string){
  const char *p;

  for (p = string; *p != '\0'; p++){
    continue;
  }
  return p - string;
}



void main(void){

  char a[] = "leaf\0";
  char b[] = "helloWorld/relative/leaf\0";
  char c[] = "/root/stuff/leaf\0";
  char d[] = "///////leaf\0";
  char e[] = "/../../../../../../../..\0";
  char f[] = "\0";
  char g[] = "/root/stuff/\0";
  char h[] = "rel/stuff/\0";
  char j[] = "//grow.p\0";
 
  printf("len1 of a %u\n", strlen1(a));
  
  printf("last letter of leaf is %s\n", a+strlen1(a)-1);

  char *ptrs[9] = {a,b,c,d,e,f,g,h,i};

  int relative;

  printf("%u\n", sizeof(true));
  printf("%d\n", (int)true);

  int i;
  for (i = 0; i < 8; i ++){
    char *ptr1;
    char *leaf1;
    printf("full %s\n",ptrs[i]);
    char buf [strlen(ptrs[i])+1];
    memcpy(buf, ptrs[i], strlen(ptrs[i]) + 1);
    
    relative = dir_path_and_leaf (buf, &ptr1 , &leaf1);
    
    if(relative){
      if(ptr1){
	if(leaf1){
	  printf("rel path is %s, leaf is %s\n", ptr1, leaf1);
	  printf("leaf %p, leaf - buf %u string from ptr %s\n", leaf1, leaf1 - buf, ptrs[i]+(leaf1-buf)); 
	}else{
	  printf("invalid rel path \n");
	}
      }else{
	if(leaf1){
	  printf("put in CWD, leaf is %s\n", leaf1);
	  printf("leaf %p, leaf - buf %u string from ptr %s\n", leaf1, leaf1 - buf, ptrs[i]+(leaf1-buf)); 
	}else{
	  printf("invalid rel path \n");
	}
      }
    }else{
      if(ptr1){
	if(leaf1){
	  printf("abs path is %s, leaf is %s\n", ptr1, leaf1);
	  printf("leaf %p, leaf - buf %u string from ptr %s\n", leaf1, leaf1 - buf, ptrs[i]+(leaf1-buf)); 
	}else{
	  printf("invalid abs path \n");
	}
      }else{
	if(leaf1){
	  printf("put in root, leaf is %s\n", leaf1);
	  printf("leaf %p, leaf - buf %u string from ptr %s\n", leaf1, leaf1 - buf, ptrs[i]+(leaf1-buf)); 
	}else{
	  printf("invalid abs path \n");
	}
      }
    }
  }
}
