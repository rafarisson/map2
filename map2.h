/**
	@file map2.h
	@brief Header map2
	
	Mapas com controle de acesso via mutex, permitindo acesso seguro em modo
	somente leitura ou leitura/escrita.
	
	Modo somente leitura: a tarefa que precisa apenas consultar dados do mapa
	reserva o acesso apenas para copiar esses dados, logo em seguida o acesso
	ser� liberado para as demais tarefas.
	Neste modo o acesso ao mapa fica reservado por um curto per�odo de tempo.
	Outras tarefas poder�o alterar os dados do mapa, por�m os dados n�o ser�o
	replicados para o item copiado. Dados alterados no item copiado tamb�m n�o
	ser�o replicados para o mapa.
	
	Modo leitura/escrive: o mapa permanece reservado para a tarefa que
	requisitou o acesso, liberando o acesso para as demais tarefas somente ap�s
	concluir a utiliza��o dos dados.
	Meste modo o acesso ao mapa fica reservado pelo tempo em que a tarefa
	necessite utilizar os dados. Nenhuma outra tarefa ir� alterar os dados do
	mapa antes que o acesso seja liberado.
	
	@note Fun��es, macros e tipos com prefixo '__' foram projetados apenas para
	uso interno e n�o devem ser utilizados
	
	@note Fun��es, macros e tipos que contenham 'unsafe' n�o s�o seguros e,
	preferencialmente, n�o devem ser utilizados. Exceto na inicializa��o do mapa
	com map2_init(..), onde poder� ser utilizado map2_unsafe_foreach(..) para
	inicializar cada item do mapa.
	Mesmo assim garanta que n�o haver� outras tarefas acessando o mapa al�m da
	tarefa que esta inicializando.
*/

#ifndef __MAP2_H__
#define __MAP2_H__

#include <RTL.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifndef MAP2_OS_MUT_CREATE
#define MAP2_OS_MUT_CREATE(MNAME, NKEYS)	static OS_MUT __##MNAME##_mut [NKEYS];
#endif

#ifndef MAP2_OS_MUT_INIT
#define MAP2_OS_MUT_INIT(M, KEY)			os_mut_init((void*)map2_ptr((M)->mut, (KEY), void))
#endif

#ifndef MAP2_OS_MUT_TAKE
#define MAP2_OS_MUT_TAKE(M, KEY, TOUT)		(os_mut_wait(map2_ptr((M)->mut, (KEY), void), (TOUT)) == OS_R_TMO)
#endif

#ifndef MAP2_OS_MUT_DROP
#define MAP2_OS_MUT_DROP(M, KEY)			os_mut_release(map2_ptr((M)->mut, (KEY), void))
#endif

/**
	Tipo de dados correspondente ao mapa
	Ponteiros void permite, que os itens do mapa sejam de tipo customizado
	
	@note N�o crie manualmente, utilize MAP2(..)
*/
typedef struct {
	const void *data;		/** Ponteiro para o mapa */
	const int rows;			/** N�mero de linhas */
	const int columns;		/** N�mero de colunas */
	const int data_size;	/** Tamanho total do mapa */
	const int field_size;	/** Tamanho de um item */
	const void *mut;		/** Ponteiro para o mapa de mutex */
	const int keys;			/** Quantidade de chaves dispon�veis */
}
map2_t;

/**
	Modos de opera��o
*/
typedef enum {
	MAP2_OP_READONLY = 0,
	MAP2_OP_READWRITE,
}
map2_operation_t;

/**
	O mapa pode ser acesso por mais de uma tarefa ao mesmo tempo, desde que, os
	itens acessados sejam diferentes
	Podemos disponibilizar tr�s chaves de acesso para tr�s tarefas diferentes -
	ou tr�s inst�ncias da mesma tarefa -, separando o acesso a itens referentes
	aos canais pares, �mpares e de expans�o
	
	@def MAP2_NKEYS_1 Um acesso simult�neo ao mapa (qualquer canal)
	@def MAP2_NKEYS_2 Dois acessos simult�neos ao mapa (canais pares e �mpares)
	@def MAP2_NKEYS_3 Tr�s acessos simult�neos ao mapa (canais pares, �mpares e
	expans�o)
*/
#define MAP2_NKEYS_1		(1)
#define MAP2_NKEYS_2		(2)
#define MAP2_NKEYS_3		(3)

/**
	@brief Macro para cria��o de mapa com tipo e tamanho de dados customizados
	
	@param data_type Tipo de dado do mapa
	@param mapname Nome do mapa
	@param nrows Quantidade de linhas
	@param ncolumns Quantidade de colunas
	@param nkeys Quantidade de chaves para controle de acesso

	@note Quantidade de linhas/colunas deve ser, no m�nimo, 1
	@note Quantidade de chaves para controle de acesso deve ser, no m�nimo, 1
	
	Exemplo:
		typedef struct {
			int a;
			int b;
		}
		t_t;
		MAP2(t_t, my_map1, SLOT_MAX * SLOT_CH, SLOT_DEVICES, MAP2_NKEYS_3);
*/
#define MAP2(data_type, mapname, nrows, ncolumns, nkeys)	\
	static data_type __##mapname [nrows][ncolumns];			\
	#ifndef MAP2_CONFIG_MUT_DISABLE							\
	static OS_MUT __##mapname##_mut [nkeys]; 				\
	#endif													\
	map2_t mapname = { 										\
		.data = __##mapname, 								\
		.rows = nrows,										\
		.columns = ncolumns,								\
		.data_size = sizeof(__##mapname),					\
		.field_size = sizeof(data_type),					\
		.mut = __##mapname##_mut,							\
		.keys = nkeys,										\
	};

/**
	@brief Macro para importar mapas apenas pelo nome
*/
#define MAP2_IMPORT(mapname) \
	extern map2_t mapname;

void __map2_init(const map2_t *m);

/**
	@brief Inicializa��o b�sica do mapa
	
	@param m Endere�o do mapa
	@param fnc Fun��o de inicializa��o
	
	Essa fun��o inicializa apenas o mutex de controle de acesso ao mapa
	Outros dados podem ser iniciados implementando 'func', por exemplo:
		map2_init(&my_map1, {
			map2_unsafe_foreach(&my_map1, item, t_t) {
				item->a = 0;
				item->b = 1;
			}
		});
	Ou utilizando fun��o de inicializa��o, por exemplo:
		map2_init(&my_map1, my_map_init(&my_map1));
*/
#define map2_init(m, fnc) \
	__map2_init(m); \
	fnc;

/**
	@brief La�o para cada elemento de um mapa
	
	@param m Endere�o do mapa
	@param item Item (dispon�vel apenas dentro do la�o)
	@param type Tipo de dados do mapa
	
	@note N�o seguro! O controle de acesso n�o � utilizado
*/
#define map2_unsafe_foreach(m, item, type) \
	for (type *item = (type*)(m)->data; item != NULL && item < (type*)((uint32_t)(m)->data + (m)->data_size); item++)

/**
	@brief Retorna a posi��o da linha para um item no mapa
	
	@param m Endere�o do mapa
	@param item Item (dispon�vel apenas dentro do la�o)
	@param type Tipo de dados do mapa
	
		map2_unsafe_foreach(&my_map1, item, t_t) {
			int row = map2_item_row(&my_map1, item, t_t);
		}
	
	@note Para ser usado com map2_unsafe_foreach()
*/
#define map2_item_row(m, item, type) \
	((item - (type*)(m)->data) / (m)->columns)

/**
	@brief Retorna a posi��o da coluna para um item no mapa
	
	@param m Endere�o do mapa
	@param item Item (dispon�vel apenas dentro do la�o)
	@param type Tipo de dados do mapa
	
		map2_unsafe_foreach(&my_map1, item, t_t) {
			int column = map2_item_column(&my_map1, item, t_t);
		}
	
	@note Para ser usado com map2_unsafe_foreach()
*/
#define map2_item_column(m, item, type) \
	((item - (type*)(m)->data) % (m)->columns)

int map2_key(const map2_t *m, int row);
void __map2_drop(const map2_t *m, int row, int column, int key);
void *__map2_take(const map2_t *m, int row, int column, int key, void *dst, uint32_t tout, map2_operation_t op);

/**
	@brief Acesso seguro para leitura de um item no mapa
	
	@param m Endere�o do mapa
	@param row Posi��o do item na linha
	@param column Posi��o do item na coluna
	@param key Poisi��o da chave de acesso
	@param dst Item (destido onde os dados do item ser�o copiados)
	@param tout Timeout de acesso
	@param fnc Fun��o executada quando o item estiver dispon�vel
	@param err Fun��o executada quando ocorrer erro no acesso
	
	Exemplo:
		for (int c = 0; c < 3; c++) {
			for (int n = 0; n < 4; n++) {
				int key = task_instance;
				t_t data_ro = {0};
				map2_readonly_trycatch(&my_map1, c, n, key, data_ro, 2000, {
					int a = dara_ro.a;
					int b = dara_ro.b;
				},{
					break;
				});
			}
		}
	
	@note Mesmo que o item seja modificado dentro de 'func', seus dados n�o
	ser�o replicados para o mapa
	
	@note O acesso ao mapa � requisitado ao chamar map2_readonly*() e liberado o
	mais r�pido poss�vel, antes de executar o bloco de c�digo implementado em
	'fnc'
	
	@note Se o bloco de c�digo 'err' for executado, o acesso ao mapa n�o foi
	alocado
*/
#define map2_readonly_trycatch(m, row, column, key, dst, tout, fnc, err)	\
	if (__map2_take(m, row, column, key, &dst, tout, MAP2_OP_READONLY) != NULL) { \
		fnc; \
	} else { \
		err; \
	}
#define map2_readonly_try(m, row, column, key, dst, tout, fnc) \
	map2_readonly_trycatch(m, row, column, key, dst, tout, fnc, {})

#define map2_readonly_trycatch2(m, row, column, key, dst, tout, fnc, err)	\
	t_t __map2_tmp_data = {0}; \
	if (__map2_take(m, row, column, key, &__map2_tmp_data, tout, MAP2_OP_READONLY) != NULL) { \
		dst = (const t_t*)&__map2_tmp_data; \
		fnc; \
	} else { \
		err; \
	}

/**
	@brief Acesso seguro para escrita/leitura de um item no mapa
	
	@param m Endere�o do mapa
	@param row Posi��o do item na linha
	@param column Posi��o do item na coluna
	@param key Poisi��o da chave de acesso
	@param dst Ponteiro para item (acesso direto ao mapa)
	@param tout Timeout de acesso
	@param fnc Fun��o executada quando o item estiver dispon�vel
	@param err Fun��o executada quando ocorrer erro no acesso
	
	Exemplo:
		for (int c = 0; c < 3; c++) {
			for (int n = 0; n < 4; n++) {
				int key = task_instance;
				t_t *dara_rw;
				map2_readwrite_trycatch(&my_map1, c, n, key, dara_rw, 2000, {
					dara_rw->a *= 10;
					dara_rw->b *= 10;
				},{
					break;
				});
			}
		}
	
	@note O item � um ponteiro para o mapa
	
	@note O acesso ao mapa � requisitado ao chamar map2_readwrite*() e liberado
	ap�s executar o bloco de c�digo implementado em 'fnc'
	
	@note Se o bloco de c�digo 'err' for executado, o acesso ao mapa n�o foi
	alocado
*/
#define map2_readwrite_trycatch(m, row, column, key, dst, tout, fnc, err) \
	if ((dst = __map2_take(m, row, column, key, dst, tout, MAP2_OP_READWRITE)) != NULL) { \
		fnc; \
		__map2_drop(m, row, column, key); \
	} else { \
		err; \
	}
#define map2_readwrite_try(m, row, column, key, dst, tout, fnc) \
	map2_readwrite_trycatch(m, row, column, key, dst, tout, fnc, {})

#endif
