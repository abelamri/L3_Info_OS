#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


/**********************************************************
** Codage d'une instruction (32 bits)
***********************************************************/

typedef struct {
	unsigned OP: 10;  /* code operation (10 bits)  */
	unsigned i:   3;  /* nu 1er registre (3 bits)  */
	unsigned j:   3;  /* nu 2eme registre (3 bits) */
	short    ARG;     /* argument (16 bits)        */
} INST;


/**********************************************************
** definition de la memoire simulee
***********************************************************/

typedef int WORD;  /* un mot est un entier 32 bits  */

WORD mem[128];     /* memoire                       */


/**********************************************************
** Codes associes aux instructions
***********************************************************/

#define INST_ADD	(0)
#define INST_SUB	(1)
#define INST_CMP	(2)
#define INST_IFGT   (3)
#define INST_NOP    (4)
#define INST_JUMP   (5)
#define INST_HALT   (6)
#define INST_SYSC   (7)
#define INST_LOAD   (8)
#define INST_STORE  (9)


/**********************************************************
** Placer une instruction en memoire
***********************************************************/

void make_inst(int adr, unsigned code, unsigned i, unsigned j, short arg) {
	union { WORD word; INST fields; } inst;
	inst.fields.OP  = code;
	inst.fields.i   = i;
	inst.fields.j   = j;
	inst.fields.ARG = arg;
	mem[adr] = inst.word;
}


/**********************************************************
** Codes associes aux interruptions
***********************************************************/

#define INT_INIT	(1)
#define INT_SEGV	(2)
#define INT_INST	(3)
#define INT_TRACE	(4)
#define INT_CLOCK	(5)
#define INT_HALT	(6)
#define INT_SYSC	(7)


/**********************************************************
** Arguments système
***********************************************************/

#define SYSC_EXIT       (1)
#define SYSC_PUTI       (2)
#define SYSC_NEW_THREAD (3)


/**********************************************************
** Le Mot d'Etat du Processeur (PSW)
***********************************************************/

typedef struct PSW {    /* Processor Status Word */
	WORD PC;        /* Program Counter */
	WORD SB;        /* Segment Base */
	WORD SS;        /* Segment Size */
	WORD IN;        /* Interrupt number */
	WORD DR[8];     /* Data Registers */
	WORD AC;        /* Accumulateur */
	INST RI;        /* Registre instruction */
} PSW;

/**********************************************************
** Codage des processus
***********************************************************/


#define MAX_PROCESS (20)   /* nb maximum de processus  */
#define EMPTY       (0)    /* processus non-pret       */
#define READY       (1)    /* processus pret           */

struct {
	PSW cpu;               /* mot d’etat du processeur */
	int state;             /* etat du processus        */
} process[MAX_PROCESS];  /* table des processus      */
int current_process = -1;  /* nu du processus courant  */

void init_process(int ind) {	
	process[ind].state = READY;
	memset (&process[ind], 0, sizeof(process[ind].cpu));
	//process[ind].cpu.PC = 0;
	process[ind].cpu.SB = 0;
	process[ind].cpu.SS = 20;

}



/**********************************************************
** Simulation de la CPU (mode utilisateur)
***********************************************************/

/* instruction d'addition */
PSW cpu_ADD(PSW m) {
	m.AC = m.DR[m.RI.i] += (m.DR[m.RI.j] + m.RI.ARG);
	m.PC += 1;
	return m;
}

/* instruction de soustraction */
PSW cpu_SUB(PSW m) {
	m.AC = m.DR[m.RI.i] -= (m.DR[m.RI.j] + m.RI.ARG);
	m.PC += 1;
	return m;
}

/* instruction de comparaison */
PSW cpu_CMP(PSW m) {
	m.AC = (m.DR[m.RI.i] - (m.DR[m.RI.j] + m.RI.ARG));
	m.PC += 1;
	return m;
}

/* instruction de no operation */
PSW cpu_NOP(PSW m) {
	printf("no operation.\n");
	m.PC += 1;
	return m;
}

/* instruction de comparaison >*/
PSW cpu_IFGT(PSW m) {
	if(m.AC > 0) m.PC = (m.DR[m.RI.i] + m.RI.ARG);
	else m.PC += 1;
	
	return m;
}

/* instruction de saut */
PSW cpu_JUMP(PSW m) {
	m.PC = (m.DR[m.RI.i] + m.DR[m.RI.j] + m.RI.ARG);
	return m;
}

/* instruction d'arrêt */
PSW cpu_HALT(PSW m) {
	printf("HALT\n");
	// m.IN = INT_HALT;
	// m.PC += 1;
	process[current_process].state = !READY;
	int are_all_dead = 1;
	for (int i = 0; i < MAX_PROCESS; ++i)
		if (process[current_process].state == READY) are_all_dead = 0;
	if(are_all_dead) exit(0);
	do {
		current_process = (current_process + 1) % MAX_PROCESS;
	} while (process[current_process].state == READY);
	return process[current_process].cpu;
}

/*instruction load*/
PSW cpu_LOAD(PSW m) {
	//m.AC = m.DR[m.RI.j] + m.RI.ARG;
	m.AC = m.RI.j + m.RI.ARG;

	if(m.AC < 0 || m.AC >= m.SS) {
		printf("segv + %d\n", m.SS);
		m.IN = INT_SEGV;
		return m;
	}
	m.AC = mem[m.SB + m.AC];
	m.DR[m.RI.i] = m.AC; //on hésite
	// printf(" AC : %d ");
	m.PC += 1;
	return m;
}

/*instruction store*/
PSW cpu_STORE(PSW m) {
	m.AC = m.RI.j + m.RI.ARG;

	if(m.AC < 0 || m.AC >= m.SS) {
		printf("segv + %d\n", m.SS);
		m.IN = INT_SEGV;
		return m;
	}
	mem[m.SB + m.AC] = m.RI.i;
	m.AC = m.RI.i;
	m.PC += 1;
	return m;
}

/* instruction système */
PSW cpu_SYSC(PSW m) {
	m.IN = INT_SYSC;
	m.PC += 1;
	return m;
}

int counter = 0;

/* Simulation de la CPU */
PSW cpu(PSW m) {
	union { WORD word; INST in; } inst;
	
	
	/*** lecture et decodage de l'instruction ***/
	if (m.PC < 0 || m.PC >= m.SS) {
		m.IN = INT_SEGV;
		return (m);
	}
	inst.word = mem[m.PC + m.SB];
	m.RI = inst.in;
	/*** execution de l'instruction ***/
	switch (m.RI.OP) {
	case INST_ADD:
		m = cpu_ADD(m);
		break;
	case INST_SUB:
		m = cpu_SUB(m);
		break;
	case INST_CMP:
		m = cpu_CMP(m);
		break;
	case INST_NOP:
		m = cpu_NOP(m);
		break;
	case INST_HALT:
		m = cpu_HALT(m);
		//return m;
		break;
	case INST_JUMP:
		m = cpu_JUMP(m);
		break;
	case INST_IFGT:
		m = cpu_IFGT(m);
		break;
	case INST_SYSC:
		counter++;
		m = cpu_SYSC(m);
		return m;
	case INST_LOAD:
		counter++;
		m = cpu_LOAD(m);
		return m;
	case INST_STORE:
		counter++;
		m = cpu_STORE(m);
		return m;
	default:
		/*** interruption instruction inconnue ***/
		m.IN = INT_INST;
		return (m);
	}

	/*** interruption apres chaque instruction ***/
	counter++;
	if(counter % 3 == 0) m.IN = INT_CLOCK;
	else m.IN = INT_TRACE;

	return m;
}


/**********************************************************
** Demarrage du systeme
***********************************************************/

void systeme_init(void) {
	printf("Booting.\n");
	/*** Préparation premiers processus ***/
	current_process = 0;
/*	process[0].state = READY;
	process[1].state = READY;*/
	/*** creation d'un programme ***/
//	make_inst(0, INST_SUB, 2, 2, -1000); /* R2 -= R2-1000 */
//	make_inst(0, 5, 2, 2, -1000); /* instruction inconnue */
//	make_inst(1, INST_ADD, 1, 2, 500);   /* R1 += R2+500 */
//	make_inst(2, INST_ADD, 0, 2, 200);   /* R0 += R2+200 */
//	make_inst(3, INST_ADD, 0, 1, 100);   /* R0 += R1+100 */	
	// make_inst(4, INST_CMP, 2, 1, 0);     /* AC = (R1 - R2) 1500 - 1000*/
	// make_inst(5, INST_IFGT, 4, 0, 20);   /* if R1 > R2, PC = R4 + 11*/
	// make_inst(6, INST_NOP, 0, 0, 0);
	// make_inst(7, INST_SYSC, 2, 0, SYSC_PUTI);
	// make_inst(8, INST_SYSC, 3, 0, SYSC_PUTI);
	// make_inst(10, INST_SYSC, 3, 0, SYSC_PUTI);
	// make_inst(11, INST_SYSC, 0, 0, SYSC_EXIT);
//	make_inst(20, INST_HALT, 0, 0, 0);
	// make_inst(0, INST_SUB, 0, 0, 0); /*SUB R0, R0, 0*/
	// make_inst(1, INST_STORE, 0, 0, 100);
	// make_inst(1+1, INST_SYSC, 1, 1, SYSC_NEW_THREAD); /* nouveau thread*/
	// make_inst(2+1, INST_IFGT, 0, 0, 5+1); /*père reprend à n° 5*/
	//fils
	// make_inst(, INST_ADD, 0, 0, 666);
	// make_inst(3+1, INST_STORE, 0, 0, 1); /* R0 += R0 + 500 */
	// make_inst(, INST_LOAD, 1, 0, 1);
	// make_inst(4+1, INST_SYSC, 1, 0, SYSC_PUTI); /* affiche R0 = 500 */
	//père
	// make_inst(5+1, INST_ADD, 0, 0, 1000);/* R0 += R0 + 1000 */
	// make_inst(6+1, INST_SYSC, 0, 0, SYSC_PUTI); /* affiche R0 = 1000 */
	// make_inst(7+1, INST_SYSC, 0, 0, SYSC_EXIT); /*arrêt système */

	make_inst(0, INST_ADD, 0, 0, 500);
	make_inst(1, INST_SYSC, 0, 0, SYSC_PUTI);
	make_inst(2, INST_STORE, 0, 0, 0);
	make_inst(3, INST_SYSC, 0, 0, SYSC_PUTI);
	make_inst(4, INST_SYSC, 0, 0, SYSC_NEW_THREAD);
	make_inst(5, INST_IFGT, 0, 0, 7);
	//fils
	make_inst(6, INST_LOAD, 0, 0, 0);
	make_inst(7, INST_ADD, 0, 0, 500);
	make_inst(8, INST_STORE, 0, 0, 0);
	//pere
	make_inst(6, INST_NOP, 0, 0, 0);
	make_inst(7, INST_NOP, 0, 0, 0);
	make_inst(8, INST_NOP, 0, 0, 0);
	make_inst(9, INST_NOP, 0, 0, 0);
	make_inst(10, INST_LOAD, 0, 0, 0);
	make_inst(11, INST_SYSC, 0, 0, SYSC_PUTI);
	make_inst(12, INST_SYSC, 0, 0, SYSC_EXIT);


	//r0 = 500
	//store r0
	//thread
	//fils
	//load 0
	//add r0 500
	//store r0
	//pere
	//puty r0

	init_process(0);
}


/**********************************************************
** Simulation du systeme (mode systeme)
***********************************************************/

	WORD PC;        /* Program Counter */
	WORD SB;        /* Segment Base */
	WORD SS;        /* Segment Size */
	WORD IN;        /* Interrupt number */
	WORD DR[8];     /* Data Registers */
	WORD AC;        /* Accumulateur */
	INST RI;        /* Registre instruction */



void sysc (PSW m) {
	int new_process = current_process;
	int loop_counter = 0;

	switch(m.RI.ARG) {
		case SYSC_EXIT :
			printf("Arrêt du système.\n");
			exit(EXIT_SUCCESS);
		case SYSC_PUTI :
			printf("R%d = %d\n", m.RI.i, m.DR[m.RI.i]);
			break;
		case SYSC_NEW_THREAD :
			do {
				printf("_________________________loopc ont %d\n", loop_counter);
				loop_counter++;
				new_process = (new_process + 1) % MAX_PROCESS;
			} while (process[new_process].state == READY && loop_counter < MAX_PROCESS);
			
			if (loop_counter == MAX_PROCESS) {
				printf("Tous les processus sont déjà occupés.\n");
			}
			else {
				printf("Création d'un nouveau thread %d.\n", new_process);
				init_process(new_process);
				process[new_process].cpu = m;
				process[current_process].cpu.RI.i = new_process;
				process[current_process].cpu.AC = new_process;
				process[new_process].cpu.RI.i = 0;
				process[new_process].cpu.AC = 0;
			}
			break;
	}
}

PSW systeme(PSW m) {
	// if (m.IN != INT_TRACE)
	// 	printf("Interruption n° : %d\n", m.IN);
	printf("Process %d : Program Counter : %d.\n", current_process, m.PC);
	switch (m.IN) {
		case INT_INIT:
			systeme_init();
			return process[current_process].cpu;
		case INT_SEGV:
			printf("Interruption de segmentation.\n");
			exit(1);
			break;
		case INT_TRACE:
			break;
		case INT_INST:
			printf("Erreur : Instruction inconnue.\n");
			exit(1);
			break;
		case INT_HALT:
			printf("HALT.\n");
			exit(0);
			break;
		case INT_SYSC :
			printf("Appel système ...\n");
			sysc(m);
			if (counter % 3 != 0) break;
			else m.IN = INT_CLOCK;

		case INT_CLOCK :
			printf("Interruption clock\n");			
			process[current_process].cpu = m;
			printf("Changement de processus\n");
			do {
				current_process = (current_process + 1) % MAX_PROCESS;
			} while (process[current_process].state == READY);
			printf("Nouveau processus : %d\n", current_process);
			return process[current_process].cpu;
			break;

	}
	return m;
}


/**********************************************************
** fonction principale
** (ce code ne doit pas etre modifie !)
***********************************************************/

int main(void) {
	PSW mep;
	
	mep.IN = INT_INIT; /* interruption INIT */	
	while (1) {
		//sleep(1);
		mep = systeme(mep);
		mep = cpu(mep);
	}
	
	return (EXIT_SUCCESS);
}

