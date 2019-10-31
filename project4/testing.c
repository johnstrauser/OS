int main () {
    const char * path = "home/documents/apps/a.txt";
    struct inode * inode;
    get_node_by_path(path, 0);
    return 0;
}

int get_node_by_path(const char *path, int ino) {
	char str [1024];
	str[0] = '/';
    str[1] = '\0';
	int result = 0;
	int counter = 0;
	while (counter < strlen(path)) {
		if ((path[counter] == '/' && counter != 0)) {
			printf("%s\n", str);
		}
		char arr [2];
		arr[0] = path[counter];
		arr[1] = '\0';
		strcat(str, arr);
		counter++;
        if (counter == strlen(path)) {
            char arr [2];
            arr[0] = path[counter];
            arr[1] = '\0';
            strcat(str, arr);
            printf("%s\n", str);
        }
	}
    return 0;
}