#!/bin/bash


file_log="./testout.log"
rm -f $file_log

# flag controllo esisti
test1=0 
test2=0

# Lancio delle batterie di test

# **** 50 istanze che effettuano la batteria di test 1 ****
for (( i = 1; i <= 50; i++)) do
		./client client_$i 1 &>> $file_log &
	done
# *********************************************************

test1=1


echo "----- Lanciato primo segnale SIGUSR1 al server -----" #prima che termino tutti i client precedenti
killall -10 objectstore

wait #attendo che termino


# **** 30 istanze effettuano la batteria di test 2 ****

for (( i = 1; i <= 30; i++)) do
	./client client_$i 2 &>> $file_log &
done

# ******************************************************


# **** 30 istanze effettuano la batteria di test 2 ****

for (( i = 31; i <= 50; i++)) do
	./client client_$i 3 &>> $file_log &
done

# ******************************************************
wait #attendo che termino
test2=1



echo "----- Lanciato secondo segnale SIGUSR1 al server -----"
killall -10 objectstore


# 	termino il server
killall -QUIT objectstore


# Interpretazione del log file

sleep 1

if [[ ! -r $file_log ]]; then
	echo "Error: File non esistente o non leggibile"
	exit 1
fi

IFS=$'\n'

echo "---------- Interpretazione di $file_log ----------"
line=($(awk '{print $1}' $file_log | sort -u)) #line è ora un array di tutti i nomi dei client e sono unici e ordinati

printf "\n\033[30;48;5;15m%10s%15s%13s%16s%14s%13s%14s   \033[0m\n" "CLIENT" "REGISTER" "STORE" "RETRIEVE" "DELETE" "LEAVE" "TOTALE"

#itero per ogni client
for client_name in ${line[@]}; do

	#numeri di REGISTER effettuate dal client con successo e con errore
	nreg_succ=$(grep "\<$client_name\>.*REGISTER success" $file_log | wc -l)
	nreg_err=$(grep "\<$client_name\>.*REGISTER error" $file_log | wc -l)

	#numeri di STORE effettuate dal client con successo e con errore
	nstore_succ=$(grep "\<$client_name\>.*STORE success" $file_log | wc -l)
	nstore_err=$(grep "\<$client_name\>.*STORE error" $file_log | wc -l)

	#numeri di RETRIEVE effettuate dal client con successo e con errore
	nret_succ=$(grep "\<$client_name\>.*RETRIEVE success" $file_log | wc -l)
	nret_err=$(grep "\<$client_name\>.*RETRIEVE error" $file_log | wc -l)

	#numeri di DELETE effettuate dal client con successo e con errore
	ndel_succ=$(grep "\<$client_name\>.*DELETE success" $file_log | wc -l)
	ndel_err=$(grep "\<$client_name\>.*DELETE error" $file_log | wc -l)

	#numeri di LEAVE effettuate dal client con successo e con errore
	nlea_succ=$(grep "\<$client_name\>.*LEAVE success" $file_log | wc -l)
	nlea_err=$(grep "\<$client_name\>.*LEAVE error" $file_log | wc -l)


	#numero di operazioni totali effettuate dal client
	nop=$(($nreg_succ+$nreg_err+$nstore_succ+$nstore_err+$nret_succ+$nret_err+$ndel_succ+$ndel_err+$nlea_succ+$nlea_err))
	nsucc=$(($nsucc+$nreg_succ+$nstore_succ+$nret_succ+$ndel_succ+$nlea_succ))
	nerr=$(($nerr+$nreg_err+$nstore_err+$nret_err+$ndel_err+$nlea_err))

	#salvo il numero di operazioni finora effettuate
	ntotop=$(($ntotop+$nop))

	printf "%11s%8d (%d,%d)%8d (%d,%d)%8d (%d,%d)%8d (%d,%d)%8d (%d,%d)%12d\n" \
	 ${client_name} $(($nreg_succ+$nreg_err)) $nreg_succ $nreg_err $(($nstore_succ+$nstore_err)) $nstore_succ $nstore_err \
	 $(($nret_succ+$nret_err)) $nret_succ $nret_err $(($ndel_succ+$ndel_err)) $ndel_succ $ndel_err \
	 $(($nlea_succ+$nlea_err)) $nlea_succ $nlea_err $nop
done

echo
echo -n "Batteria 1 eseguita con "
if [[ $test1 ]]; then
	echo "successo."
else
	echo "insuccesso."
fi

echo -n "Batteria 2 eseguita con "
if [[ $test2 ]]; then
	echo "successo."
else
	echo "insuccesso."
fi

echo
echo Client totali: ${#line[@]}
echo Operazioni totali: $ntotop \($nsucc successi, $nerr errori\)
echo -n "Sono stati allocati "
du -sh ./data | cut -f -1