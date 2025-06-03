<script lang="ts">
	import { onMount, onDestroy } from 'svelte';
	import Chart from 'chart.js/auto';
	import 'chartjs-adapter-date-fns';
	import {
		createRxNostr,
		createRxBackwardReq,
		uniq,
		type RxNostr,
		type EventPacket
	} from 'rx-nostr';

	import type { Subscription } from 'rxjs';
	import { verifier } from '@rx-nostr/crypto';

	// 型定義
	interface WeatherData {
		timestamp: number;
		temperature: number | null;
		humidity: number | null;
		pressure: number | null;
	}

	interface ChartCollection {
		temperature?: Chart;
		humidity?: Chart;
		pressure?: Chart;
	}

	// Nostr公開鍵
	const PUBKEY_HEX: string = '86fd1c80c07debbc3d1929377b24d4bf65a85af268af15cda2acce454df670be';

	// リレーURL
	const RELAYS: string[] = [
		'wss://yabu.me',
		'wss://relay-jp.nostr.wirednet.jp',
		'wss://nfrelay.app'
	];

	// リアクティブな状態 - Svelte5のruneを使用
	let weatherData = $state<WeatherData[]>([]);
	const rxNostr: RxNostr = createRxNostr({
		verifier
	});

	const req = createRxBackwardReq();
	let subscription: Subscription | null = null;
	let charts: ChartCollection = {};

	let connectedRelays = $state(0);
	let totalEvents = $state(0);
	let latestTemp = $state('--');
	let latestHumidity = $state('--');
	let latestPressure = $state('--');

	// DOM要素の参照
	let tempChartCanvas: HTMLCanvasElement | undefined = $state();
	let humidityChartCanvas: HTMLCanvasElement | undefined = $state();
	let pressureChartCanvas: HTMLCanvasElement | undefined = $state();

	// チャートが初期化されたかどうかのフラグ
	let chartsInitialized = $state(false);

	// テキストから天気データを抽出
	function parseWeatherData(content: string, createdAt: number): WeatherData | null {
		const data: WeatherData = {
			timestamp: createdAt * 1000, // ミリ秒に変換
			temperature: null,
			humidity: null,
			pressure: null
		};

		// 温度の抽出 (🌡️ 温度: 22.7°C や おんど: 23.0℃)
		const tempMatch = content.match(/(?:🌡️\s*)?(?:温度|おんど)[:：]?\s*([0-9.]+)\s*(?:°C|℃)/i);
		if (tempMatch) {
			data.temperature = parseFloat(tempMatch[1]);
		}

		// 湿度の抽出 (💧 湿度: 74.8% や しつど: 49%)
		const humidityMatch = content.match(/(?:💧\s*)?(?:湿度|しつど)[:：]?\s*([0-9.]+)\s*%/i);
		if (humidityMatch) {
			data.humidity = parseFloat(humidityMatch[1]);
		}

		// 気圧の抽出 (📊 気圧: 996.4hPa や きあつ: 1010hPa)
		const pressureMatch = content.match(/(?:📊\s*)?(?:気圧|きあつ)[:：]?\s*([0-9.]+)\s*hPa/i);
		if (pressureMatch) {
			data.pressure = parseFloat(pressureMatch[1]);
		}

		// 少なくとも1つのデータがある場合のみ返す
		if (data.temperature !== null || data.humidity !== null || data.pressure !== null) {
			return data;
		}

		return null;
	}

	// チャートを初期化
	function initializeCharts(): void {
		// DOM要素が存在しない場合は初期化しない
		if (!tempChartCanvas || !humidityChartCanvas || !pressureChartCanvas) {
			console.log('Canvas elements not ready');
			return;
		}

		const commonOptions = {
			responsive: true,
			maintainAspectRatio: false,
			plugins: {
				legend: {
					display: false
				}
			},
			scales: {
				x: {
					type: 'time' as const,
					time: {
						unit: 'hour' as const,
						stepSize: 1,
						displayFormats: {
							hour: 'M/d HH:mm',
							day: 'M/d',
							week: 'M/d',
							month: 'M月'
						}
					},
					ticks: {
						maxTicksLimit: 12,
						source: 'auto' as const
					},
					title: {
						display: true,
						text: '時刻'
					}
				},
				y: {
					beginAtZero: false,
					title: {
						display: true
					}
				}
			}
		};

		try {
			// 温度チャート
			charts.temperature = new Chart(tempChartCanvas, {
				type: 'line',
				data: {
					datasets: [
						{
							label: '温度 (°C)',
							data: [],
							borderColor: '#ff6b6b',
							backgroundColor: 'rgba(255, 107, 107, 0.1)',
							tension: 0.4,
							fill: true
						}
					]
				},
				options: {
					...commonOptions,
					scales: {
						...commonOptions.scales,
						y: {
							...commonOptions.scales.y,
							title: {
								display: true,
								text: '温度 (°C)'
							}
						}
					}
				}
			});

			// 湿度チャート
			charts.humidity = new Chart(humidityChartCanvas, {
				type: 'line',
				data: {
					datasets: [
						{
							label: '湿度 (%)',
							data: [],
							borderColor: '#4ecdc4',
							backgroundColor: 'rgba(78, 205, 196, 0.1)',
							tension: 0.4,
							fill: true
						}
					]
				},
				options: {
					...commonOptions,
					scales: {
						...commonOptions.scales,
						y: {
							...commonOptions.scales.y,
							min: 0,
							max: 100,
							title: {
								display: true,
								text: '湿度 (%)'
							}
						}
					}
				}
			});

			// 気圧チャート
			charts.pressure = new Chart(pressureChartCanvas, {
				type: 'line',
				data: {
					datasets: [
						{
							label: '気圧 (hPa)',
							data: [],
							borderColor: '#45b7d1',
							backgroundColor: 'rgba(69, 183, 209, 0.1)',
							tension: 0.4,
							fill: true
						}
					]
				},
				options: {
					...commonOptions,
					scales: {
						...commonOptions.scales,
						y: {
							...commonOptions.scales.y,
							title: {
								display: true,
								text: '気圧 (hPa)'
							}
						}
					}
				}
			});

			chartsInitialized = true;
			console.log('Charts initialized successfully');

			// 初期化完了後にデータがあれば更新
			if (weatherData.length > 0) {
				updateCharts();
			}
		} catch (error) {
			console.error('Chart initialization error:', error);
		}
	}

	// チャートを更新
	function updateCharts(): void {
		if (!chartsInitialized) {
			console.log('Charts not initialized yet');
			return;
		}

		console.log('Updating charts with data:', weatherData.length, 'entries');

		const sortedData = [...weatherData].sort((a, b) => a.timestamp - b.timestamp);

		// 温度データ
		const tempData = sortedData
			.filter((d) => d.temperature !== null)
			.map((d) => ({ x: d.timestamp, y: d.temperature! }));

		// 湿度データ
		const humidityData = sortedData
			.filter((d) => d.humidity !== null)
			.map((d) => ({ x: d.timestamp, y: d.humidity! }));

		// 気圧データ
		const pressureData = sortedData
			.filter((d) => d.pressure !== null)
			.map((d) => ({ x: d.timestamp, y: d.pressure! }));

		console.log('Chart data prepared:', {
			tempData: tempData.length,
			humidityData: humidityData.length,
			pressureData: pressureData.length
		});

		// チャート更新
		if (charts.temperature && tempData.length > 0) {
			charts.temperature.data.datasets[0].data = tempData;
			charts.temperature.update('active');
		}

		if (charts.humidity && humidityData.length > 0) {
			charts.humidity.data.datasets[0].data = humidityData;
			charts.humidity.update('active');
		}

		if (charts.pressure && pressureData.length > 0) {
			charts.pressure.data.datasets[0].data = pressureData;
			charts.pressure.update('active');
		}

		// 統計情報更新
		updateStats();
	}

	// 統計情報を更新
	function updateStats(): void {
		totalEvents = weatherData.length;

		if (weatherData.length === 0) {
			latestTemp = '--';
			latestHumidity = '--';
			latestPressure = '--';
			return;
		}

		// 最新のデータを取得
		const latest = weatherData.reduce((latest, current) =>
			current.timestamp > latest.timestamp ? current : latest
		);

		latestTemp = latest.temperature !== null ? latest.temperature.toFixed(1) : '--';
		latestHumidity = latest.humidity !== null ? latest.humidity + '%' : '--';
		latestPressure = latest.pressure !== null ? latest.pressure.toFixed(1) : '--';
	}

	// 天気データを追加する関数
	function addWeatherData(newData: WeatherData): void {
		// 重複チェック
		const exists = weatherData.some((d) => Math.abs(d.timestamp - newData.timestamp) < 1000);

		if (!exists) {
			// Svelte5のリアクティビティのために新しい配列を作成
			weatherData = [...weatherData, newData];
			console.log('Weather data added:', newData);
			console.log('Total weather data entries:', weatherData.length);

			// チャートが初期化されている場合のみ更新
			if (chartsInitialized) {
				updateCharts();
			}
		}
	}

	// Nostrイベントを監視
	function subscribeToEvents(): void {
		// 過去7日間のフィルター（より長期間のデータを取得）
		const since = Math.floor((Date.now() - 7 * 24 * 60 * 60 * 1000) / 1000);

		console.log('Starting event subscription with filter:', {
			authors: [PUBKEY_HEX],
			kinds: [1],
			since: since,
			limit: 500 // より多くのイベントを取得
		});

		// イベントを購読
		subscription = rxNostr
			.use(req)
			.pipe(uniq())
			.subscribe({
				next: (packet: EventPacket) => {
					console.log('Received event:', {
						id: packet.event.id,
						content: packet.event.content,
						created_at: packet.event.created_at
					});

					// 天気データを抽出
					const weatherInfo = parseWeatherData(packet.event.content, packet.event.created_at);
					if (weatherInfo) {
						addWeatherData(weatherInfo);
					}
				},
				error: (error: Error) => {
					console.error('Subscription error:', error);
				},
				complete: () => {
					console.log('Subscription completed');
				}
			});

		// フィルターを送信
		req.emit({
			authors: [PUBKEY_HEX],
			kinds: [1],
			since: since,
			limit: 500 // より多くのイベントを取得
		});
	}

	// 接続状態を監視
	rxNostr.createConnectionStateObservable().subscribe({
		next: (packet) => {
			console.log('Connection state packet:', packet);
			// 接続中のリレー数を更新
			const connected = RELAYS.filter((relay) => {
				const status = rxNostr?.getRelayStatus(relay);
				return status?.connection === 'connected';
			}).length;

			connectedRelays = connected;
			console.log(`Connected relays: ${connectedRelays}/${RELAYS.length}`);
		},
		error: (error) => {
			console.error('Connection state error:', error);
		}
	});

	// Svelte5のエフェクト - weatherDataが変更されたときにチャートを更新
	$effect(() => {
		if (chartsInitialized && weatherData.length > 0) {
			console.log('Effect triggered: updating charts with', weatherData.length, 'entries');
			updateCharts();
		}
	});

	// コンポーネントマウント時
	onMount(() => {
		console.log('Component mounted');
		rxNostr.setDefaultRelays(RELAYS);

		// DOM要素の準備を待ってからチャートを初期化
		const initCharts = () => {
			if (tempChartCanvas && humidityChartCanvas && pressureChartCanvas) {
				initializeCharts();
			} else {
				console.log('Waiting for canvas elements...');
				setTimeout(initCharts, 100);
			}
		};

		// 少し遅らせてから初期化開始
		setTimeout(initCharts, 100);

		// イベント購読開始
		subscribeToEvents();
	});

	// コンポーネント破棄時
	onDestroy(() => {
		console.log('Component destroying');

		// サブスクリプションを停止
		if (subscription) {
			subscription.unsubscribe();
		}

		// チャートを破棄
		Object.values(charts).forEach((chart) => {
			if (chart) {
				chart.destroy();
			}
		});

		// rxNostrを停止
		if (rxNostr) {
			rxNostr.dispose();
		}
	});
</script>

<main>
	<div class="container">
		<div class="header">
			<h1>🌡️まいへやデータビューア</h1>
			<a
				class="underline"
				href="https://lumilumi.app/myRoom@tsukemonogit.github.io"
				rel="noreferrer noopener"
				target="_blank"
			>
				Nostrまいへやアカウント
			</a>
		</div>

		<div class="controls">
			<div class="status">
				<div class="status-indicator {connectedRelays > 0 ? 'connected' : ''}"></div>
				<span class="status-text">
					接続中 ({connectedRelays}/{RELAYS.length} リレー)
				</span>
			</div>
			<div class="debug-info">
				<small>
					デバッグ: {weatherData.length}件のデータ, チャート初期化: {chartsInitialized
						? '完了'
						: '未完了'}
				</small>
			</div>
		</div>

		<div class="stats">
			<div class="stat-card">
				<div class="stat-number">{totalEvents}</div>
				<div class="stat-label">総イベント数</div>
			</div>
			<div class="stat-card">
				<div class="stat-number">{latestTemp}</div>
				<div class="stat-label">最新温度 (°C)</div>
			</div>
			<div class="stat-card">
				<div class="stat-number">{latestHumidity}</div>
				<div class="stat-label">最新湿度 (%)</div>
			</div>
			<div class="stat-card">
				<div class="stat-number">{latestPressure}</div>
				<div class="stat-label">最新気圧 (hPa)</div>
			</div>
		</div>

		<div class="charts">
			<div class="chart-container">
				<div class="chart-header">
					<h3>🌡️ 温度の推移</h3>
				</div>
				<div class="chart-content">
					<canvas bind:this={tempChartCanvas}></canvas>
				</div>
			</div>

			<div class="chart-container">
				<div class="chart-header">
					<h3>💧 湿度の推移</h3>
				</div>
				<div class="chart-content">
					<canvas bind:this={humidityChartCanvas}></canvas>
				</div>
			</div>

			<div class="chart-container">
				<div class="chart-header">
					<h3>📊 気圧の推移</h3>
				</div>
				<div class="chart-content">
					<canvas bind:this={pressureChartCanvas}></canvas>
				</div>
			</div>
		</div>
		<div class="footer">
			<a
				href="https://github.com/TsukemonoGit/M5MyRoomNostr"
				target="_blank"
				rel="noreferrer noopener"
				class="underline">GitHub</a
			>
		</div>
	</div>
</main>

<style>
	:global(*) {
		margin: 0;
		padding: 0;
		box-sizing: border-box;
	}

	:global(body) {
		font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
		background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
		min-height: 100vh;
		padding: 12px;
	}

	.container {
		max-width: 1200px;
		margin: 0 auto;
		background: white;
		border-radius: 15px;
		box-shadow: 0 20px 40px rgba(0, 0, 0, 0.1);
		overflow: hidden;
	}

	.header {
		background: linear-gradient(45deg, #667eea, #764ba2);
		color: white;
		padding: 30px;
		text-align: center;
	}

	.header h1 {
		font-size: 2.5rem;
		margin-bottom: 10px;
	}

	.controls {
		padding: 20px 30px;
		background: #f8f9fa;
		border-bottom: 1px solid #e9ecef;
	}

	.status {
		display: flex;
		align-items: center;
		gap: 15px;
		margin-bottom: 15px;
	}

	.status-indicator {
		width: 12px;
		height: 12px;
		border-radius: 50%;
		background: #dc3545;
		transition: background 0.3s;
	}

	.status-indicator.connected {
		background: #28a745;
		animation: pulse 2s infinite;
	}

	@keyframes pulse {
		0%,
		100% {
			opacity: 1;
		}
		50% {
			opacity: 0.5;
		}
	}

	.status-text {
		font-weight: 500;
		color: #495057;
	}

	.debug-info {
		color: #6c757d;
		font-size: 0.9rem;
	}

	.stats {
		display: grid;
		grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
		gap: 20px;
		padding: 20px 30px;
	}

	.stat-card {
		background: #f8f9fa;
		padding: 20px;
		border-radius: 10px;
		text-align: center;
		border: 2px solid transparent;
		transition: border-color 0.3s;
	}

	.stat-card:hover {
		border-color: #667eea;
	}

	.stat-number {
		font-size: 2rem;
		font-weight: bold;
		color: #667eea;
		margin-bottom: 5px;
	}

	.stat-label {
		color: #6c757d;
		font-size: 0.9rem;
	}

	.charts {
		padding: 30px;
	}

	.chart-container {
		margin-bottom: 40px;
		background: white;
		border-radius: 10px;
		box-shadow: 0 5px 15px rgba(0, 0, 0, 0.1);
		overflow: hidden;
	}

	.chart-header {
		background: #f8f9fa;
		padding: 20px;
		border-bottom: 1px solid #e9ecef;
	}

	.chart-header h3 {
		color: #495057;
		font-size: 1.3rem;
		margin: 0;
	}

	.chart-content {
		padding: 20px;
		position: relative;
		height: 400px;
	}

	.footer {
		text-align: center;
		padding: 20px;
		background: #f8f9fa;
		border-top: 1px solid #e9ecef;
	}
	.footer a {
		color: #495057;
		text-decoration: none;
		font-weight: 500;
	}
	.footer a:hover {
		text-decoration: underline;
	}
</style>
