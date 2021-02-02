#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

using namespace std;
string::size_type sz;

//Input for reading in the file
typedef struct input{
    char PC[128];
    char opt[128];
    char dest_reg[128];
    char src1_reg[128];
    char src2_reg[128];

} line_feed;
char line[128];
int counter = 0;
long issue_rate = 0;

//Original idea was to store in vectors, still use the size for conditions
// but decided a struct was superior
vector<input> trace_in;
vector<string> PC_VEC;
vector<string> opt_vec;
vector<string> dest_vec;
vector<string> src1_vec;
vector<string> src2_vec;

//This contains most of the information that will be passed into a 1024 sized circular list
struct ROB{

    long pipe_line;
    bool src_status_1= true;
    bool src_status_2 = true;
    bool op_status= true;
    long tag;

    long operation;
    long src1=0;
    long src2=0;
    long dest=0;

    // if->0, id->1, is->2, ex->3, wb->4
    long cycle[5];
    long delay[5];

    long cycle_time;
    long entry;
    long reg1_entry;
    long reg2_entry;

    struct ROB *next;
    struct ROB *last;

};

//register files
struct RegisterFile
{
    long tag;
    bool valid = true;
};

//variables are constantly passed around in each function, so they are made global for programming convinience
long 	cycle_final=0;
long 	tag=0;
long    N=0;
long	S;
long 	clock_cycle = 0;
//counters for moving items in and out of ROB
long 	rob_index = 0;
long    rob_index_id = 0;
//used for functional unit logic
long	func_num;
long    schedule;
struct  RegisterFile rf[1024];//Register File

float IPC;

//fake rob and a psuedo rob are created for passing completed/retired instructions through
//temp is also used to temporarily store items
struct ROB fake_rob[1024];
struct ROB *head;
struct ROB *psuedo_rob;
struct ROB *temp_rob;
struct ROB  *tail;
int output = 0;

FILE	    *tracefile;
const char  *tracename;

char	prog_count;
int		op;
int     dest;
int     src1;
int     src2;

//circular list for dispatch, issue and execute that tracks the fake ROB
struct function_list{
    long disp_list=0;
    long issue_list=0;
    long exe_list=0;
    long tag;
    struct function_list *next;
    struct function_list *last;
};

function_list tracking[1024];

//ROB and lists are initalized here
void initialize(){
    head= fake_rob;
    tail=head;
    fake_rob[1023].next=&fake_rob[0];
    fake_rob[0].last=&fake_rob[1023];

    tracking[1023].next=&tracking[0];
    tracking[0].last=&tracking[1023];

    for(int i=0;i<1023;i++)
    {
        fake_rob[i].next=&fake_rob[i+1];
        fake_rob[i+1].last=&fake_rob[i];

    }

    for(int i=0;i<1024;i++)
    {
        fake_rob[i].entry=i;
    }
    for(int i =0; i<1023; i++){
        tracking->tag = i;
        tracking[i].next=&tracking[i+1];
        tracking[i+1].last=&tracking[i];
    }
    temp_rob=head;
    head->last=NULL;
}

//printing out is delegated here to make testing more convienient
void print_out(){
    cout<<temp_rob->tag;
    cout<<" fu{" <<temp_rob->operation<<"} ";
    cout<<" src{" <<temp_rob->src1<<',' << temp_rob->src2<<"} ";
    cout<<" dst{" <<temp_rob->dest<<"} ";
    cout<<" IF{" <<temp_rob->cycle[0]<<',' << temp_rob->delay[0]<<"} ";
    cout<<" ID{" <<temp_rob->cycle[1]<<',' << temp_rob->delay[1]<<"} ";
    cout<<" IS{" <<temp_rob->cycle[2]<<',' << temp_rob->delay[2]<<"} ";
    cout<<" EX{" <<temp_rob->cycle[3]<<',' << temp_rob->delay[3]<<"} ";
    cout<<" WB{" <<temp_rob->cycle[4]<<',' << temp_rob->delay[4]<<"} "<<endl;
}

//Fake retire as was recommended in the MP3 document
void fake_retire() {
    long temp = temp_rob->cycle[4] + temp_rob->delay[4];
    if (head->pipe_line> 5){
        head = head->next;
    }

    //remove instructions from the head of the rob and increment to the next one
    if (psuedo_rob->pipe_line== 5)//reach WB, completed
    {
        psuedo_rob->pipe_line=100;//Set to large number to indicate completion
        psuedo_rob->delay[4] = 1;
        if (temp_rob->pipe_line> 5){
            if(temp_rob->tag== output && output < trace_in.size()) {
                print_out();
                if (cycle_final < temp){
                    cycle_final = temp;}
                output++;
                temp_rob = temp_rob->next;
            }
        }
    }
}

//Execute as was recommended in the MP3 document
void execute(){
    if (psuedo_rob->cycle_time == 0 && psuedo_rob->pipe_line==4)
    {
        {
            tracking->exe_list = 0;
            psuedo_rob->pipe_line=5;
            func_num++;
            if(psuedo_rob->dest >=0)
            {
                if(psuedo_rob->entry==rf[psuedo_rob->dest].tag)
                {
                    rf[psuedo_rob->dest].valid=true;
                }
            }
            psuedo_rob->op_status=true;
            psuedo_rob->cycle[4]=clock_cycle;
            long cycle_diff = psuedo_rob->cycle[4] - psuedo_rob->cycle[3];
            psuedo_rob->delay[3]=cycle_diff;
        }
    }
    else if(psuedo_rob->pipe_line==4)
        psuedo_rob->cycle_time--;//depend on op type
}

//Issue as was recommended in the MP3 document
void issue() {
    if (psuedo_rob->pipe_line == 3 && issue_rate>0)//
    {
        //Added for readability
        long index_one = psuedo_rob->reg1_entry;
        long index_two = psuedo_rob->reg2_entry;
        if (fake_rob[index_one].op_status == true || psuedo_rob->src_status_1 == true) {
            if ((fake_rob[index_two].op_status == true || psuedo_rob->src_status_2 == true)) {
                psuedo_rob->cycle_time = psuedo_rob->cycle_time - 1;
                issue_rate=issue_rate-1;
                psuedo_rob->pipe_line = 4;
                schedule = schedule + 1;
                func_num--;
                tracking->issue_list = 0;
                tracking->exe_list = 1;
                //cout<<func_num<<endl;
                psuedo_rob->cycle[3] = clock_cycle;
                long cycle_diff = psuedo_rob->cycle[3] - psuedo_rob->cycle[2];
                psuedo_rob->delay[2] = cycle_diff;
            }
        }
    }
}

//dispatch as was recommended in the MP3 document
void dispatch() {
    if (schedule > 0) {
        if(schedule>S){
            schedule = S;
        }
        if(func_num > N+1){
            func_num = N+1;
        }
        if (psuedo_rob->pipe_line == 2 && schedule>0) {
            schedule = schedule - 1;
            rob_index_id = rob_index_id + 1;
            tracking->disp_list = 0;
            tracking->issue_list = 1;

            psuedo_rob->pipe_line = 3; //state=3
            psuedo_rob->cycle[2] = clock_cycle;

            long cycle_diff = psuedo_rob->cycle[2] - psuedo_rob->cycle[1];
            psuedo_rob->delay[1] = cycle_diff;

        }
    }
    if (schedule == 0) {
    }
}
//fetch as was recommended in the MP3 document
void fetch(){
    if(psuedo_rob->pipe_line==1&&rob_index_id>0)
    {
        tracking->exe_list = 0;
        rob_index = rob_index+1;
        psuedo_rob->pipe_line=2;
    }
}

//general logic loop for items not in the advanced cycle
void normal_cycle(){
    for(psuedo_rob=head;psuedo_rob!=tail;psuedo_rob=psuedo_rob->next) {
        fake_retire();
        execute();
        issue();
        dispatch();
        fetch();
    }
}
//File reader
void read_file(const char *file_in, int pos_flag){
    if(pos_flag == 1) {
        FILE *ifp;
        ifp = fopen(tracename, "r");
        input lineFeed;
        tracefile=fopen(tracename,"r");

        //Read file and save it to a vector
        while (fgets(line, sizeof(line), ifp) != NULL) {
            int num_matches = sscanf(line, "%s %s %s %s %s", lineFeed.PC, lineFeed.opt, lineFeed.dest_reg,
                                     lineFeed.src1_reg, lineFeed.src2_reg);
            if (num_matches != 5) {
            } else

                trace_in.push_back(lineFeed);
            //used in testing of code
            PC_VEC.emplace_back(lineFeed.PC);
            opt_vec.emplace_back(lineFeed.opt);
            dest_vec.emplace_back(lineFeed.dest_reg);
            src1_vec.emplace_back(lineFeed.src1_reg);
            src2_vec.emplace_back(lineFeed.src2_reg);
        }
    }
    if (pos_flag == 2){

        fscanf(tracefile,"%s %d %d %d %d\n",&prog_count, &op,&dest,&src1,&src2);
        tail->tag=tag;
        tail->src1=src1;
        tail->src2=src2;
        tail->dest=dest;
    }
}

//This was seperated for the sake of testing
void register_access(){
    if ( rf[tail->src1].valid == false && tail->src1>0)
    {
        tail->reg1_entry=rf[tail->src1].tag;
        tail->src_status_1 = false;
    }
    else if(rf[tail->src1].valid == true)
    {
        tail->src_status_1=true;
        tail->reg1_entry=tail->entry;
    }

    else if(tail->src1==-1){
        tail->src_status_1=true;
        tail->reg1_entry=tail->entry;
    }

    if(!rf[tail->src2].valid && tail->src2!=-1)
    {

        tail->reg2_entry=rf[tail->src2].tag;// else  tail->reg2_entry=0;
        tail->src_status_2 = false;
    }
    else if(rf[tail->src2].valid)
    {
        tail->src_status_2=true;
        tail->reg2_entry=tail->entry;
    }
    else if(tail->src2==-1){
        tail->src_status_2=true;
        tail->reg2_entry=tail->entry;
    }
}

//Advanced cycle as was described in the document
void advanced_cycle(){

    while( rob_index_id > 0 && rob_index > 0)
    {
        rob_index = rob_index -1;
        rob_index_id = rob_index_id - 1;

        read_file(tracename, 2);
        tail->operation=op;
        //delay logic
        switch(tail->operation){
            case 0:
                tail->cycle_time=1;
                break;

            case 1:
                tail->cycle_time=2;
                break;

            case 2:
                tail->cycle_time=5;
                break;
        }
        tail->delay[0]=1;
        tail->pipe_line=1;//IF

        tail->cycle[0]=clock_cycle;
        tail->cycle[1]=clock_cycle + 1;
        tail->op_status=false; //not completed

        register_access();

        rf[tail->dest].tag = tail->entry;//entry means the entry of fake_rob
        rf[tail->dest].valid = false;
        tag++;
        tail=tail->next;
    }
    counter++;
}


int main( int argc, char *argv[])//int argc,char *argv[]
{
    string s_in = argv[1];
    string n_in = argv[2];
    tracename   = argv[3];

    S = stoi(s_in, &sz);
    N = stoi(n_in, &sz);

    read_file(tracename, 1);


    initialize();

    //Assign items that are immutable here, indexes are made for items here
    schedule = S;
    func_num=N+1;
    rob_index=N;
    rob_index_id=N*2;

    //while loop that makes sure the code is running
    while(output<trace_in.size())
    {
        issue_rate = N+1;
        normal_cycle();   //This deals with parts 1-5 in the instructions
        advanced_cycle(); //This deals with the 6th section
        clock_cycle++;      //This counts clock cycles until the end

    }

    //logic for printing out IPC as desired
    IPC=(float)output/(float)cycle_final;
    if(IPC < 1) {
        IPC = int(IPC * 100000);
        IPC = float(IPC/100000);
    }
    cout<<"number of instructions = "<<output<<endl;
    cout<<"number of cycles       = "<<cycle_final<<endl;
    cout<<"IPC                    = "<<IPC<<endl;

    return(0);
}