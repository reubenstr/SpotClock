<?php 
 // get spot prices from providentmetals.com api
 // parse minimal required data and display results in JSON format
 // providentmetals.com updates every 60 seconds
?>
<?php
 $array = json_decode(file_get_contents('https://www.providentmetals.com/services/spot/summary.USD.json'), true); 
 //print_r($array);
 
 // round to two decimal places and add max two trailing zeros
 $auSpot = sprintf('%0.2f',round($array[0]['rate'], 2));
 $agSpot = sprintf('%0.2f',round($array[1]['rate'], 2)); 
 $auDelta = sprintf('%0.2f',round($array[0]['delta'], 2));
 $agDelta = sprintf('%0.2f',round($array[1]['delta'], 2));
 
 // manually build JSON Object 
 echo '{"au": "' . $auSpot . '","auDelta": "' . $auDelta . '","ag": "' . $agSpot . '","agDelta": "' . $agDelta .'","time": "' . $array[1]['effective_at'] .'"}'; 
?>