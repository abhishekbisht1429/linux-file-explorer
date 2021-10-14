#include<iostream>
#include<vector>
#include<string>

#include<termios.h>
#include<fcntl.h>
#include<unistd.h>
#include<sys/ioctl.h>
#include<errno.h>
#include<signal.h>
#include<stdlib.h>
#include<string.h>
#include<dirent.h>
#include<sys/types.h>
#include<sys/stat.h>

using namespace std;

#define TRUE 1
#define FALSE 0

struct cursor_pos {
    int i, j;
};

struct entry {
    string name;
    size_t size;
    string owner;
    string permission;
    string last_mod;
};

/*
    Global Configuration Variables
*/
struct termios tty_config;
struct winsize tty_ws;
const char *tty = "/dev/tty";
/* file descriptor for tty */
int fd;

/*
    Global Variables specific to application
*/
/* application root */
string root;
/* cursor position */
cursor_pos cpos;
/* status bar start position */
cursor_pos st_start;
/* index of first file to be displayed */
int top;
/* index of last file to be displayed */
int bottom;
/* current working directory */
string cwd;
/* array to store enteries of files in current dir */
vector<entry> enteries;

/*
    functions dealing with content rendering
*/
void render() {
    /* clear screen */
    cout<<"\e[1;1H\e[2J";

    cout<<"bottom "<<bottom<<"\n";
    /* render content */
    for(int i=top; i<=bottom; ++i) {
        cout<<i<<"\n";
        cout<<enteries[i].name<<" "
            <<enteries[i].size<<" "
            <<enteries[i].owner<<" "
            <<enteries[i].permission<<" "
            <<enteries[i].last_mod<<"\n";
    }

    /* move cursor to correct pos */
    // cout<<"\x1B["<<cpos.i<<";"<<cpos.j<<"H";
}

void icanon(int activate) {
    if(activate)
        tty_config.c_lflag |= ICANON;
    else
        tty_config.c_lflag &= ~ICANON;
    tcsetattr(fd, TCSANOW, &tty_config);
}

/* store all info about files of current 
    directory in enteries array */
void list() {
    DIR *temp = opendir(cwd.c_str());
    if(temp == NULL) {
        printf("unable to open current directory\n");
        return;
    }
    enteries.clear();

    dirent *ent;
    while((ent = readdir(temp)) != NULL) {
        string fname = ent->d_name;
        string abs_path = cwd + "/" + fname;
        
        struct stat st;
        stat(abs_path.c_str(), &st);

        entry e;
        e.name = fname;
        e.size = st.st_size;
        e.owner = to_string(st.st_uid);
        /* extract file type and permisison info */
        if(S_ISREG(st.st_mode)) e.permission += "-";
        else if(S_ISBLK(st.st_mode)) e.permission += "b";
        else if(S_ISCHR(st.st_mode)) e.permission += "c";
        else if(S_ISDIR(st.st_mode)) e.permission += "d";
        else if(S_ISFIFO(st.st_mode)) e.permission += "p";
        else if(S_ISLNK(st.st_mode)) e.permission += "l";
        else if(S_ISSOCK(st.st_mode)) e.permission += "s";

        e.permission += (st.st_mode & S_IRUSR)?'r':'-';
        e.permission += (st.st_mode & S_IWUSR)?'w':'-';
        e.permission += (st.st_mode & S_IXUSR)?'x':'-';

        e.permission += (st.st_mode & S_IRGRP)?'r':'-';
        e.permission += (st.st_mode & S_IWGRP)?'w':'-';
        e.permission += (st.st_mode & S_IXGRP)?'x':'-';

        e.permission += (st.st_mode & S_IROTH)?'r':'-';
        e.permission += (st.st_mode & S_IWOTH)?'w':'-';
        e.permission += (st.st_mode & S_IXOTH)?'x':'-';
        /* extract last modified time */
        char buf[100];
        strftime(buf, sizeof buf, "%e %b %Y %H:%M:%S", localtime(&st.st_atim.tv_sec));
        e.last_mod += string(buf, strlen(buf));
        enteries.push_back(e);
    }
    top = 0;
    bottom = min((size_t)tty_ws.ws_row, enteries.size())-1;
    cout<<enteries.size()<<"\n";
}

/* Handle tty window resize */
void signal_win_resize(int signum) {
    if(ioctl(fd, TIOCGWINSZ, &tty_ws) < 0) {
        fprintf(stderr, "failed to obtain new size after window resize\n");
        exit(1);
    }
    // printf("winsize : %d %d\n", tty_ws.ws_row, tty_ws.ws_col);

    /* re calculate status bar position */
    st_start.i = tty_ws.ws_row;
    st_start.j = 1;
}

/*
    Main function
*/
int main() {
    /* open /dev/tty */
    fd = open(tty, O_RDWR | O_NDELAY | O_NOCTTY);
    if(!isatty(fd)) {
        fprintf(stderr, "failed to open tty\n");
        return 0;
    }
    
    /* get the current tty configuration */
    if(tcgetattr(fd, &tty_config)<0) {
        fprintf(stderr, "unable to get tty config\n");
        return 0;
    }

    /* get window size config */
    if(ioctl(fd, TIOCGWINSZ, &tty_ws) < 0) {
        int error = errno;
        fprintf(stderr, "unable to get window size\n");
        fprintf(stderr, "%d\n", error);
        return 0;
    }

    /* set signal for window resize */
    signal(SIGWINCH, signal_win_resize);

    /* Enter normal mode */
    icanon(FALSE);

    /* get root and current working directory */
    root = getcwd(NULL, 0);
    cwd = root;

    list();
    render();

    while(1)
        pause();


    // write(fd, cwd, strlen(cwd));
    // write(fd, "\n", 1);

    // DIR *temp = opendir(cwd);
    // struct dirent *ent;
    // while((ent = readdir(temp)) != NULL)
    //     printf("%s\n", ent->d_name);


    // tty_config.c_lflag &= ~(ECHO);
    // tcsetattr(fd, TCSANOW, &tty_config);

    // printf("winsize : %d %d\n", tty_ws.ws_row, tty_ws.ws_col);

    // write(fd, "hello\n", 6);

    // write(fd, "asdf\n", 5);
    // write(fd, "\033[H\033[J", 6);
    // // write(fd, "\e[1;1H\e[2J", 12);
}