#include "map2.h"

#define DBG_MODULE "map2"
#include "shared/dbg.h"

/**
	Macros assert
	
	Exemplos:
		
		MAP2_ASSERT(ptr == NULL, return false);
		MAP2_ASSERT(ptr == NULL, return);
		MAP2_ASSERT(ptr == NULL, {
			printr("null ptr");
			return false;
		});
	
	
*/
#define MAP2_ASSERT(cond, ret)		if (cond) ret;

/**
	Definições para habilitar mensagens de debug
	
	@def MAP2_CONFIG_DBG_WAIT Habilita mensagem quando aguardar mutex
	@def MAP2_CONFIG_DBG_TIMEOUT Habilita mensagem de timeout no mutex
	@def MAP2_CONFIG_DBG_TAKE Habilita mensagem de uso do mutex
	@def MAP2_CONFIG_DBG_DROP Habilita mensagem de liberação do mutex
*/
#define MAP2_CONFIG_DBG_WAIT
#define MAP2_CONFIG_DBG_TIMEOUT
#define MAP2_CONFIG_DBG_TAKE
#define MAP2_CONFIG_DBG_DROP

/**
	@def map2_ptr Ponteiro um campo no mapa
	@def map2_val Valor de um campo no mapa
	@def map2_pos Cálculo de posição pela linha e coluna
	
	Os dados sempre são organizados como um vetor simples, como se
	houvesse apenas uma linha (a menos que algum muito estranho aconteça):
	[r0-c0][r0-c1][r0-c2] [r1-c0][r1-c1][r1-c2] [r2-c0][r2-c1][r2-c2]
	Tudo isso alinhado conforme a arquitetura
	Assim, podemos fazer o acesso ao mapa através de um ponteiro simples
*/
#define map2_ptr(var, pos, type)	(type*)((uint32_t)var + pos)
#define map2_val(var, pos, type)	*map2_ptr(var, pos, type)
#define map2_pos(m, row, column)	((column + (m->columns * row)) * m->field_size)


/**
	@brief Retorna a posição da chave de acesso com base na configuração do mapa
	
	@param m Endereço do mapa
	@param row Posição do item na linha
	
	@note A posição indica o acesso conforme os canais configurados em hardware,
	como descrito na tabela abaixo:
	
		NKYES / Retorno		0		1		2
		MAP2_NKEYS_1		T		X		X
		MAP2_NKEYS_2		P		I		X
		MAP2_NKEYS_3		P		I		E
	
	Legenda:
		T= Todos os canais
		P= Canais pares
		I= Canais ímpares
		E= Canais expansão
		X= Inválido
		
	@note Válido somente se UART_INSTANCES == 2 para canais da placa base (pares
	e ímpares) e, instância simples para os canais de expansão
	
	@note Para ser usado com map2_readonly*() ou map2_readwrite*() para 
	selecionar a chave de acesso
	
	Exemplo:
		int key = map2_key(&my_map1, c);
		t_t data_ro = {0};
		map2_readonly_trycatch(&my_map1, c, n, key, data_ro, 2000, {
			...
		},{
			...
		});
	
*/
int map2_key(const map2_t *m, int row) {
	MAP2_ASSERT(m == NULL, return 0);
	MAP2_ASSERT(row < 0 || row >= m->rows, return 0);
	
	if (m->keys == MAP2_NKEYS_1)
		return 0;
	else if (m->keys == MAP2_NKEYS_3 && row >= SLOT_CNT * SLOT_CH)
		return 2;
	else
		return row % UART_INSTANCES == 0 ? 0 : 1;
}

/**
	@brief Inicialização dos mutex do mapa
	
	@param m Endereço do mapa
*/
void __map2_init(const map2_t *m) {
	MAP2_ASSERT(m == NULL, return);
	
	for (int k = 0; k < m->keys; k++)
		os_mut_init((void*)map2_ptr(m->mut, k, void));
}

/**
	@brief Liberação de mutex para acesso ao mapa
	
	@param m Endereço do mapa
	@param row Posição do item na linha
	@param column Posição do item na coluna
	@param key Poisição da chave de acesso
*/
void __map2_drop(const map2_t *m, int row, int column, int key) {
	MAP2_ASSERT(m == NULL, return);
	MAP2_ASSERT(row < 0 || row >= m->rows || column < 0 || column >= m->columns, return);
	MAP2_ASSERT(key < 0 || key >= m->keys, return);
	
	#ifdef MAP2_CONFIG_DBG_DROP
		dbgW("Drop row:%d column:%d key:%d task:%d\n", row, column, key, os_tsk_self());
	#endif
	
	os_mut_release(map2_ptr(m->mut, key, void));
}

/**
	@brief Aguarda e aloca mutex para acesso ao mapa
	
	@param m Endereço do mapa
	@param row Posição do item na linha
	@param column Posição do item na coluna
	@param key Poisição da chave de acesso
	@param dst Item (destido onde os dados do item serão copiados)
	@param tout Timeout de acesso
	@param op Modo de operação

	@return Ponteiro para o item ou, NULL quando ocorrer erro no acesso
*/
void *__map2_take(const map2_t *m, int row, int column, int key, void *dst, uint32_t tout, map2_operation_t op) {
	MAP2_ASSERT(m == NULL, return NULL);
	MAP2_ASSERT(row < 0 || row >= m->rows || column < 0 || column >= m->columns, return NULL);
	MAP2_ASSERT(key < 0 || key >= m->keys, return NULL);
	
	// Previne que o timeout seja maior ou igual ao valor máximo do RTOS
	// Quando timeout é 0xFFFF o RTOS assume que o tempo é infinito, fazendo 
	// os_mut_wait() a tarefa aguardar o mutex para sempre
	if (tout >= 0xFFFF)
		tout -= 1;
	
	#ifdef MAP2_CONFIG_DBG_WAIT
		dbgW("Wait row:%d column:%d key:%d task:%d timeout:%d op:%d\n", row, column, key, os_tsk_self(), tout, op);
	#endif
	
	#ifndef MAP2_CONFIG_MUT_DISABLE
		if (os_mut_wait(map2_ptr(m->mut, key, void), tout) == OS_R_TMO) {
			#ifdef MAP2_CONFIG_DBG_TIMEOUT
				dbgW("Timeout row:%d column:%d key:%d task:%d timeout:%d\n", row, column, key, os_tsk_self(), tout);
			#endif
			return NULL;
		}
	#endif
	
	void *src = map2_ptr(m->data, map2_pos(m, row, column), void);
	
	#ifdef MAP2_CONFIG_DBG_TAKE
		dbgW("Take row:%d column:%d key:%d task:%d %s\n", row, column, key, os_tsk_self(), src == NULL ? "null-ptr" : "");
	#endif
	
	if (op == MAP2_OP_READONLY) {
		// Cópia dos dados para uso no modo somente leitura
		// Isso garante que os dados alterados em 'field' não são replicados
		// para o mapa
		if (dst != NULL && src != NULL)
			memcpy(dst, src, m->field_size);
	}
	else {
		// Modo leitura e escrita, apenas aponta 'field' para o mapa, assim,
		// o acesso é diretamente no mapa
		dst = src;
	}
	
	if (dst == NULL || op == MAP2_OP_READONLY) {
		// Qualquer erro libera o mutex
		// Como vamos usar map2_readonly() ou map2_readwrite(), não precisamos
		// se preocupar com liberar o mutex em caso de erro
		__map2_drop(m, row, column, key);
	}
	
	return dst;
}
